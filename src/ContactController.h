#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <cstdint>

#include "protocol.h"

/**
 * ContactController —— 联系人 / 好友链路控制器（QML 与 core 之间的唯一入口）
 *
 * 覆盖服务端**已经实现**的 TCP 消息：
 *   搜索用户             SearchUserReq/Rsp
 *   发送好友申请         AddFriendReq/Rsp
 *   好友申请列表         SyncFriendRequestReq/Rsp
 *   被申请方推送         NotifyAddFriendReq（服务端主动推，收到后自动回拉列表）
 *   接受 / 拒绝申请      AuthFriendReq/Rsp（回包只带 error，编号从请求侧带回）
 *   申请结果推送         NotifyAuthFriendReq（对方处理了我发出的申请）
 *   好友列表             SyncFriendListReq/Rsp（1020/1021，2026-10-17 服务端补齐）
 *
 * ⚠ 文本消息（1013/1014）由 ChatController / ChatSocket 负责，与联系人域无关；
 *   本类只管"有哪些好友"——会话列表就是好友列表的投影。
 *
 * QML 侧：
 *   ContactController.searchUser(keyword)
 *   ContactController.sendFriendRequest(toUid, verifyInfo, remark, peerName)
 *   ContactController.refreshApplyList()
 *   ContactController.refreshFriendList()
 *   读 searchResults / searchResultCount / applyList / friendList / friendCount
 *     （searchResults 每项为 QVariantMap：uid / name / desc / sex / icon）
 *     （applyList 每项为 QVariantMap：id / fromUid / toUid / peerUid / received /
 *       name / verifyInfo / sex / state / stateText）
 *     （friendList 每项为 QVariantMap：uid / name / desc / nick / displayName / sex / icon）
 *
 *   ⚠ applyList 里**我发出的和我收到的混在一起**（服务端查询是
 *     `applicant_uid = ? OR receiver_uid = ?`），而 name/icon/sex 取的是对方的。
 *     UI 要显示"对面是谁"，请一律用 **peerUid**，别用 fromUid；
 *     received 表示方向（true = 别人发给我的）。
 */
class ContactController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString tip READ tip NOTIFY tipChanged)
    Q_PROPERTY(bool tipIsError READ tipIsError NOTIFY tipIsErrorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

    /// 最近一次搜索的结果列表（服务端可能命中多个同名/相似用户，可返回多条）
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchResultsChanged)
    /// 本次搜索命中条数（0 = 未找到）
    Q_PROPERTY(int searchResultCount READ searchResultCount NOTIFY searchResultsChanged)

    /// 好友申请列表，每项是一个 QVariantMap，供 Repeater / ListView 直接用
    Q_PROPERTY(QVariantList applyList READ applyList NOTIFY applyListChanged)

    /**
     * 还没看过的好友申请数（侧栏「好友申请」的红点）。
     *
     * 只数「别人发给我 + 还在等处理 + 编号大于已读水位」的：
     *   · 我已经接受 / 拒绝的立刻从计数里消失；
     *   · 我发出去的不算（那是等对方处理，不该提醒我自己）；
     *   · MySQL 申请编号自增且单调，所以"这之前我都看过"用一个水位数字就够，
     *     不用往配置里塞一长串 id（那玩意只会越长越大）。
     *
     * 打开「好友申请」分区时调 markApplyListRead() 把水位推到当前最新。
     */
    Q_PROPERTY(int unreadApplyCount READ unreadApplyCount NOTIFY unreadApplyCountChanged)

    /**
     * 好友列表（1020/1021），每项 QVariantMap：
     * uid / name / desc / nick / displayName / sex / icon
     *
     * `nick` 是**我给对方设置的备注**，`displayName` = nick 非空 ? nick : name
     * （显示名这个判断放在 C++，免得 QML 里每处都写一遍三元）。
     */
    Q_PROPERTY(QVariantList friendList READ friendList NOTIFY friendListChanged)
    Q_PROPERTY(int friendCount READ friendCount NOTIFY friendListChanged)

public:
    explicit ContactController(QObject* parent = nullptr);

    [[nodiscard]] QString tip() const { return m_tip; }
    [[nodiscard]] bool tipIsError() const { return m_tip_is_error; }
    [[nodiscard]] bool busy() const { return m_busy; }

    [[nodiscard]] QVariantList searchResults() const { return m_search_results; }
    [[nodiscard]] int searchResultCount() const { return static_cast<int>(m_search_results.size()); }

    [[nodiscard]] QVariantList applyList() const { return m_apply_list; }
    [[nodiscard]] int unreadApplyCount() const { return m_unread_apply_count; }

    [[nodiscard]] QVariantList friendList() const { return m_friend_list; }
    [[nodiscard]] int friendCount() const { return static_cast<int>(m_friend_list.size()); }

    /// 搜索用户：keyword 可以是 uid，也可以是用户名（服务端两种都认）
    Q_INVOKABLE void searchUser(const QString& keyword);

    /**
     * 发送好友申请。
     *
     * @param toUid     目标 uid（字符串形式的 uint64）
     * @param verifyInfo 验证信息
     * @param remark    备注：加好友时给对方起的别名（服务端会落库）
     * @param peerName  对方的显示名。**只用于本地那条乐观记录** ——
     *                  AddFriendRsp 只回一个 request id，不带名字；
     *                  不传的话刚发出的那条申请只能显示 uid，直到下次刷新。
     */
    Q_INVOKABLE void sendFriendRequest(const QString& toUid, const QString& verifyInfo,
                                       const QString& remark, const QString& peerName);

    /// 刷新「别人发给我的」好友申请列表
    Q_INVOKABLE void refreshApplyList();

    /**
     * 把申请列表标记成"已看过"：红点清掉，水位按 uid 存进 QSettings。
     *
     * 由界面在**真的把列表摆到眼前**时调用（进入「好友申请」分区、以及停在
     * 该分区时列表又变了）；不看就不清，否则红点等于从来不会亮。
     */
    Q_INVOKABLE void markApplyListRead();

    /**
     * 刷新好友列表（1020）。
     *
     * 与 refreshApplyList() 共用同一个 busy 门：同一时刻只让一条链路占着控制器，
     * 否则两条回包的 busyChanged 会互相踩。忙的时候这次调用被**静默丢弃**，
     * 需要补刷的场合（比如刚接受完申请）请走 \l m_friend_refresh_pending 那套排队。
     */
    Q_INVOKABLE void refreshFriendList();

    /**
     * 静默刷新好友列表：不覆盖底部那一行提示。
     *
     * 用在"用户并没有主动点刷新"的场合 —— 例如发消息收到 1022（可能是对方删了我），
     * 此时屏幕上该留着的仍是那条发送失败提示，不能被"共 N 位好友"盖掉。
     */
    Q_INVOKABLE void refreshFriendListSilently();

    /**
     * 接受 / 拒绝一条收到的好友申请。
     *
     * @param requestId 申请编号（applyList 里的 id）
     * @param state     1 = 接受，2 = 拒绝
     *
     * 回包不带编号，所以控制器和 socket 各自记着"这一发是谁"；
     * 成功的表现是 applyList 里那条的 state / stateText 就地变成已接受 / 已拒绝。
     */
    Q_INVOKABLE void handleFriendRequest(const QString& requestId, int state);

    /**
     * 判断某个 uid 现在是不是我的好友（读的是最近一次 1020/1021 的结果）。
     *
     * 用途：资料卡打开时决定要不要给「删除好友」按钮 —— 那个判断放在页面里、
     * 设置成弹窗属性传进去（弹窗是展示组件，不去读控制器）。
     */
    Q_INVOKABLE bool isFriend(const QString& uid) const;

    /**
     * 删除好友（1027 / 1028）。
     *
     * 服务端**双向删除**（双方的好友关系和备注一起删），并同步 Redis 好友集合；
     * 但**不通知被删除方** —— 对方的列表要等他自己刷新才会变。
     *
     * 成功后本地立刻把这条从 friendList 里摘掉（friendListChanged → 会话列表同步）。
     * 历史聊天记录**保留**：删除好友不删本地库，会话照旧能看到（只是发不出去了）。
     *
     * @param uid 要删的好友 uid（applyList / friendList 里的字符串形式）
     */
    Q_INVOKABLE void removeFriend(const QString& uid);

    /**
     * 设置好友备注（1029 / 1030）。
     *
     * 备注是**单向**的：只改「我给对方起的名字」，对方无感知。传空串即清空备注。
     * 服务端成功后**不推送、也不回带新备注**，所以成功后本地就地更新 friendList
     * （nick + displayName）→ friendListChanged → 会话列表跟着改名，不用等下一次 1020。
     *
     * @param uid    好友 uid（必须是好友；非好友服务端回 1022）
     * @param remark 新备注；空串 = 清空
     */
    Q_INVOKABLE void setFriendRemark(const QString& uid, const QString& remark);

    /// 清空当前搜索结果（比如用户改了关键字）
    Q_INVOKABLE void clearSearchResult();

signals:
    void tipChanged();
    void tipIsErrorChanged();
    void busyChanged();
    void searchResultsChanged();
    void applyListChanged();
    void unreadApplyCountChanged();
    void friendListChanged();

    /// 好友列表刷新结束（ok=false 时 error 是服务端 / 本地错误码）
    void friendListRefreshed(bool ok, int error);

    /**
     * 删除好友结束。
     *
     * ok=false 时 error 可能是：1022（本来就不是好友）、1013（数据库 / Redis 不可用）、
     * 5（本地等回包超时）等。
     */
    void friendDeleteFinished(bool ok, int error, const QString& uid);

    /**
     * 设置好友备注结束（1029 / 1030）。
     *
     * ok=false 时 error：1022（不是好友 / 给自己设置）、1020（uid 非法）、
     * 1013（数据库不可用）、5（本地等回包超时）。
     */
    void friendRemarkSetFinished(bool ok, int error, const QString& uid);

    /// 搜索结束；found=false 时 error 是服务端错误码（未找到通常是 1014）
    void searchFinished(bool found, int error);
    /// 好友申请发送结果
    void friendRequestSent(bool ok, int error);
    /// 处理（接受 / 拒绝）他人申请的结果；requestId / state 是请求侧带回来的
    void friendRequestAuthFinished(bool ok, int error, const QString& requestId, int state);
    /// 服务端推来「我发出的申请被处理了」（1012）；只给在线申请人推
    void friendRequestAnswered(const QString& requestId, int state);
    /// 本地校验失败，field 为 keyword / toUid / requestId
    void validationFailed(const QString& field);

private:
    void setTip(const QString& text, bool is_error = false);
    void setBusy(bool busy);

    /// 自己的 uid；未登录时为 0
    [[nodiscard]] std::uint64_t selfUid() const;

    /// 换账号后按下 uid 懒加载「已读水位」（QSettings；同一 uid 只读一次）
    void ensureSeenLoaded();

    /// 按 applyList 重算红点数（申请列表 / 登录状态一变就调）
    void refreshUnreadApplyCount();

    /**
     * 发送成功后，**立刻在本地补一条「已发出」的申请记录**。
     *
     * 为什么不等刷新：用户刚点完「加好友」，界面上必须马上能看到这条，
     * 更要紧的是 —— 弹窗要据此立刻切到「申请详情」，不再给「加好友」按钮，
     * 否则同一秒内连点就会发出多条重复申请。
     * 补出来的记录与服务端回包同形，下次 refreshApplyList() 会被真实数据覆盖。
     */
    void appendLocalSentRequest(std::uint64_t requestId);

    void onSearchResult(bool found, const QList<mychat::protocol::UserInfo>& infos, int error);
    void onFriendRequestResult(bool ok, int error, std::uint64_t requestId);
    void onFriendRequestList(const QList<mychat::protocol::FriendRequest>& requests, int error);
    void onFriendList(const QList<mychat::protocol::FriendInfo>& friends, int error);
    void onFriendDeleteResult(bool ok, int error, const QString& uid);
    void onFriendRemarkResult(bool ok, int error, const QString& uid, const QString& remark);
    void onAuthFriendResult(bool ok, int error, std::uint64_t requestId, int state);
    void onFriendRequestHandled(std::uint64_t requestId, int state);

    /**
     * 就地改某条申请的 state / stateText（服务端成功或收到推送时用）。
     *
     * 不发网络请求、也不改顺序 —— 列表是"按创建时间倒序"的，改状态不该让它跳位置。
     */
    void applyRequestState(const QString& requestId, int state);

    /**
     * 服务端推来「有人申请加你」。
     *
     * 推送只带申请记录 id，没有对方信息 —— 所以这里不直接更新界面，
     * 而是挂一个刷新意图，等控制器空闲后回拉一次 SyncFriendRequestReq。
     * 不立刻发的原因：refreshApplyList() 一遇到 busy 就整条丢弃，而"用户此刻
     * 正在搜索/发申请"是常见情形，那条推送就白收了。
     */
    void onFriendRequestNotified(std::uint64_t requestId);

    /// 把挂起的刷新意图落地（忙的时候什么都不做，由 setBusy 再喊一次）
    void flushPendingApplyRefresh();

    /// refreshApplyList() 的实现体；silent 由 m_silent_refresh 决定提示要不要覆盖
    void startApplyListRefresh();

    /// refreshFriendList() 的实现体
    void startFriendListRefresh();

    /// 把挂起的「好友列表刷新」意图落地（忙的时候什么都不做，由 setBusy 再喊一次）
    void flushPendingFriendRefresh();

    QString m_tip;
    bool m_tip_is_error = false;
    bool m_busy = false;
    /// 有推送在等「控制器空闲」→ 补一次 refreshApplyList()
    bool m_refresh_pending = false;
    /// 这次刷新是推送触发的：不要用"共 N 条"盖掉真正要说的那句话
    bool m_silent_refresh = false;

    /// 有「好友列表该刷新」的意图在等控制器空闲（接受到好友申请后就是这种）
    bool m_friend_refresh_pending = false;
    /// 这次好友列表刷新是自动触发的：不覆盖提示
    bool m_silent_friend_refresh = false;

    QVariantList m_search_results;
    QVariantList m_apply_list;
    QVariantList m_friend_list;

    /// 红点数（unreadApplyCount 的缓存值）
    int m_unread_apply_count = 0;
    /// 「这个编号之前的好友申请我都看过了」—— QSettings 里的已读水位，按 uid 分开
    qint64 m_seen_apply_id = 0;
    /// 上面这个水位属于哪个 uid（换账号要重读；空串 = 还没读过）
    QString m_seen_uid;

    // 本次「发送好友申请」的目标信息，成功后用来补那条本地记录
    std::uint64_t m_pending_peer_uid = 0;
    QString m_pending_peer_name;
    QString m_pending_verify;
    /// 一起发出去的备注（本地那条「已发出」记录的 nick 就是它）
    QString m_pending_remark;
};
