#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>

#include "protocol.h"

/**
 * GroupController —— 群聊管理链路（QML 与 core 之间的唯一入口）
 *
 * 覆盖服务端 2026-10-23（基线 `df647ad` + `7338a5c`）实现的群聊管理：
 *   创建群聊        1033 / 1034（回包带 group_id）
 *   拉人入群        1035 / 1036（只回 error）
 *   踢人            1037 / 1038（只回 error）
 *   解散群聊        1039 / 1040（只回 error）
 *   群成员信息      1041 / 1042（字段名是 `gid`）
 *   群资料获取      1043 / 1044（字段名同样是 `gid`）
 *   群资料修改      1045 / 1046（只能改 name / avatar / announcement）
 *
 * **群消息收发不在这里** —— 它复用单聊的 1013 / 1022 / 1024，
 * 归 ChatController（见 `src/ChatController.h` 的群聊说明）。
 *
 * ── 服务端的能力边界（不是本类偷懒，是接口就没有）──────────────
 *   · **没有群列表接口**：客户端只能记住"我创建的群"，以及从收到的群消息
 *     （1024 的 `group_id`）里发现新群；
 *   · **群资料要单独查（1043/1044）**：服务端没有资料变更推送，修改成功
 *     （1045/1046）也不会通知其他成员；本机 `groups` 表只是缓存，打开
 *     面板时按需调 \l loadGroupInfo 刷新；
 *   · **没有专门的成员变更推送**：入群 / 被踢 / 解散都只发一条系统消息
 *     （`from_uid = "1"`，见 api-group.md §11.5），页面收到后靠
 *     `loadMembers(gid, true)` 强刷；
 *   · 主动退群未实现，解散只有群主能做。
 *
 * QML 侧：
 *   GroupController.createGroup(name, memberUids)
 *   GroupController.addMembers(gid, memberUids)
 *   GroupController.kickMember(gid, uid)
 *   GroupController.dismissGroup(gid)
 *   GroupController.loadMembers(gid[, force]) / clearMembers()
 *   GroupController.loadGroupInfo(gid[, force]) /
 *     changeGroupInfo(gid, name, announcement)
 *   读 memberList / memberCount / gid / myRole / canManage / isOwner /
 *     groupInfo / infoLoading / busy / tip / tipIsError
 *
 *   memberList 每项 QVariantMap：uid / nickG / role / roleText / joinAt /
 *     joinAtText / displayName / isOwner / isAdmin / isMe / canKick
 *   groupInfo 是 QVariantMap：gid / name / avatar / ownerUid / announcement /
 *     maxMembers / memberCount / createAt / loaded
 */
class GroupController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString tip READ tip NOTIFY tipChanged)
    Q_PROPERTY(bool tipIsError READ tipIsError NOTIFY tipIsErrorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

    /**
     * 当前面板里的群成员（显示顺序由服务端决定，没有排序保证）。
     *
     * `displayName` 的取值顺序：**群内昵称**（服务端 `nick_g`）> 好友备注 / 昵称
     * > uid。群昵称优先是聊天软件的习惯，也是服务端唯一给的名字。
     */
    Q_PROPERTY(QVariantList memberList READ memberList NOTIFY memberListChanged)
    Q_PROPERTY(int memberCount READ memberCount NOTIFY memberListChanged)

    /**
     * 当前面板所属群的资料（1044）。
     *
     * 空 map = 还没加载；字段见类注释。`memberCount` 来自服务端的
     * `groups.member_count` 冗余列，和 \l memberCount（成员列表长度）
     * 不一致时以成员列表为准（api-group.md §9.2）。
     */
    Q_PROPERTY(QVariantMap groupInfo READ groupInfo NOTIFY groupInfoChanged)
    /// 群资料正在路上（打开面板 / 保存后刷新时显示 loading）
    Q_PROPERTY(bool infoLoading READ infoLoading NOTIFY infoLoadingChanged)

    /// 成员列表属于哪个群（空串 = 还没加载）
    Q_PROPERTY(QString gid READ gid NOTIFY memberListChanged)
    /// 我在这个群里的角色：2 群主 / 1 管理员 / 0 成员（-1 = 未知）
    Q_PROPERTY(int myRole READ myRole NOTIFY memberListChanged)
    /// 我能不能拉人 / 踢人（群主或管理员）
    Q_PROPERTY(bool canManage READ canManage NOTIFY memberListChanged)
    /// 我是不是群主（只有群主能解散）
    Q_PROPERTY(bool isOwner READ isOwner NOTIFY memberListChanged)

public:
    explicit GroupController(QObject* parent = nullptr);

    [[nodiscard]] QString tip() const { return m_tip; }
    [[nodiscard]] bool tipIsError() const { return m_tip_is_error; }
    [[nodiscard]] bool busy() const { return m_busy; }

    [[nodiscard]] QVariantList memberList() const { return m_member_list; }
    [[nodiscard]] int memberCount() const { return static_cast<int>(m_member_list.size()); }
    [[nodiscard]] QVariantMap groupInfo() const { return m_group_info; }
    [[nodiscard]] bool infoLoading() const { return m_info_loading; }
    [[nodiscard]] QString gid() const { return m_gid; }
    [[nodiscard]] int myRole() const { return m_my_role; }
    [[nodiscard]] bool canManage() const { return m_my_role >= 1; }
    [[nodiscard]] bool isOwner() const { return m_my_role == 2; }

    /**
     * 创建群聊（1033），成功后再拉人（1035）。
     *
     * 服务端的创建接口**不接受初始成员**：新群里只有群主一个人，
     * 所以这里做成两步——建群拿到 group_id，再逐个拉人。两步之间失败时，
     * 群已经建好了（里面只有自己），界面要如实说出来，别让用户以为没建成。
     *
     * @param name       群名称（服务端不校验长度，界面按 MAX_GROUP_NAME_LEN 卡）
     * @param memberUids 初始成员的 uid 列表（可空）
     */
    Q_INVOKABLE void createGroup(const QString& name, const QVariantList& memberUids);

    /// 拉人入群（1035）。成功后自动重新拉一次成员列表
    Q_INVOKABLE void addMembers(const QString& gid, const QVariantList& memberUids);

    /// 踢人（1037）。成功后自动重新拉一次成员列表
    Q_INVOKABLE void kickMember(const QString& gid, const QString& uid);

    /**
     * 解散群聊（1039）。只有群主可以。
     *
     * 成功后发 \l groupDismissed，由页面把本地会话删掉。服务端会先给所有成员
     * 写一条「该群聊已解散」系统消息（from_uid = 1）并推 1022，然后才 DEL；
     * 本机已删掉的会话由 ChatController 的 `m_removed_gids` 挡住、不会被复活。
     */
    Q_INVOKABLE void dismissGroup(const QString& gid);

    /**
     * 拉取群成员信息（1041）。打开群成员面板 / 拉人踢人成功后调用。
     *
     * @param force true = 丢掉缓存强制重拉。服务端不推送成员变化，其他客户端
     *              拉人 / 踢人后只能靠系统消息（from_uid = 1）触发一次刷新。
     */
    Q_INVOKABLE void loadMembers(const QString& gid, bool force = false);

    /**
     * 拉取群资料（1043）。
     *
     * @param force true = 丢掉缓存强制重拉；默认命中缓存时直接返回。
     *
     * 服务端不推群资料变化，本机只能按需刷新：打开面板时拉一次，
     * 保存成功后再强制拉一次（把 owner / 人数等一起刷新）。
     */
    Q_INVOKABLE void loadGroupInfo(const QString& gid, bool force = false);

    /**
     * 修改群资料（1045）。只允许群主 / 管理员（服务端存储过程判定）。
     *
     * @param name         新群名；空串在本地就挡掉（服务端会回 1020）
     * @param announcement 新公告；**空串 = 清空公告**（服务端明确支持）
     *
     * 头像暂不开放编辑：服务端没有上传通道，URL 字段填了也没地方取图。
     * 成功后发 \l groupInfoSaved，再由页面更新本地会话名（ChatStore 缓存）。
     */
    Q_INVOKABLE void changeGroupInfo(const QString& gid, const QString& name,
                                     const QString& announcement);

    /// 关掉面板时清列表（避免下次打开先闪一下上一个群的成员）
    Q_INVOKABLE void clearMembers();

    /**
     * 好友列表变化时由 main.cpp 喂进来（和 ChatController::syncFriends 同一个信号源）。
     *
     * 用途只有一个：群成员列表里显示"好友备注 > uid"。
     * 服务端 1042 只给 `nick_g` 与 uid，没有账号昵称 / 头像。
     */
    Q_INVOKABLE void syncFriends(const QVariantList& friends);

signals:
    void tipChanged();
    void tipIsErrorChanged();
    void busyChanged();
    void memberListChanged();
    void groupInfoChanged();
    void infoLoadingChanged();

    /// 建群结束（ok=false 时 error 是服务端 / 本地错误码；gid 只在成功时有值）
    void groupCreated(bool ok, int error, const QString& gid, const QString& name);
    /// 拉人结束；uids 是请求里提交的那批
    void groupMembersAdded(bool ok, int error, const QString& gid, const QStringList& uids);
    /// 踢人结束；uid 是被踢者
    void groupMemberKicked(bool ok, int error, const QString& gid, const QString& uid);
    /// 解散结束
    void groupDismissed(bool ok, int error, const QString& gid);
    /// 群资料保存成功；name 是刚提交的新群名（供页面更新会话名 / 本地缓存）
    void groupInfoSaved(const QString& gid, const QString& name);

private:
    void setTip(const QString& text, bool is_error = false);
    void setBusy(bool busy);

    void onGroupCreated(bool ok, int error, const QString& gid, const QString& name);
    void onGroupMembersAdded(bool ok, int error, const QString& gid, const QStringList& uids);
    void onGroupMemberKicked(bool ok, int error, const QString& gid, const QString& uid);
    void onGroupDismissed(bool ok, int error, const QString& gid);
    void onGroupMembersReceived(bool ok, int error, const QString& gid,
                                const QList<mychat::protocol::GroupMember>& members);
    void onGroupInfoReceived(bool ok, int error, const mychat::protocol::GroupInfo& info);
    void onGroupInfoChangeResult(bool ok, int error, const QString& gid);

    /// 把服务端的成员结构转成 QML 直接能用的 map（displayName / roleText / canKick 都在这算）
    [[nodiscard]] QVariantMap makeMemberItem(const mychat::protocol::GroupMember& member) const;

    /// 群资料 -> QML 用的 map（createAt 转成毫秒数字，其余原样）
    [[nodiscard]] QVariantMap makeGroupInfoItem(const mychat::protocol::GroupInfo& info) const;

    /// 按成员列表算我自己的角色（服务端不单独告诉我）
    void refreshMyRole();

    QString m_tip;
    bool m_tip_is_error = false;
    bool m_busy = false;

    QVariantList m_member_list;
    QString m_gid;
    int m_my_role = -1;
    /**
     * 成员列表正在路上。
     *
     * 没有这个旗子的话，同一拍里两次 loadMembers（页面在切会话时拉一次、
     * 打开面板又拉一次）会让第二次撞上 ChatSocket 的串行门，回一个 false，
     * 界面上就挂出"获取群成员失败：请稍后重试"—— 明明第一次已经成功了。
     */
    bool m_members_loading = false;

    /// uid -> 显示名（好友备注 / 昵称）；由 syncFriends 维护
    QHash<QString, QString> m_friend_names;

    /// 登录成功的 uid（字符串）：成员列表里标"（我）"、算 myRole / canKick 都靠它
    QString m_self_uid;

    /**
     * 建群后的待拉人名单。
     *
     * 服务端没有"带初始成员建群"的接口，只能建完再拉。这份名单在
     * groupCreated 回包到达时消费一次；建群失败就直接丢掉。
     */
    QStringList m_pending_members;

    /// 当前群资料（1044）；gid 记录它属于哪个群，防串台
    QVariantMap m_group_info;
    QString m_info_gid;
    bool m_info_loading = false;

    /**
     * 正在等待 1046 的那次修改。
     *
     * 只有名字需要留：保存成功后要发 groupInfoSaved(gid, name) 给页面更新
     * 本地会话名（1046 不回新资料）。公告只在日志里用，不留也行 —— 留着
     * 是为了将来排查"提交的值和刷新回来的值不一致"。
     */
    QString m_pending_name;
    QString m_pending_announcement;
};
