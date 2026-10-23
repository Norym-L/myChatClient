#include "GroupController.h"

#include <QDateTime>
#include <QTimer>
#include <QVariantMap>

#include <optional>

#include "ChatSocket.h"
#include "SessionManager.h"
#include "logger.h"
#include "protocol.h"

namespace
{
using mychat::protocol::ErrorCode;
constexpr int kSuccess = static_cast<int>(ErrorCode::Success);

/// 角色 → 中文。服务端定义：0 成员 / 1 管理员 / 2 群主（api-group.md §3.2）
QString roleText(const int role)
{
    switch (role)
    {
    case 2:
        return QStringLiteral("群主");
    case 1:
        return QStringLiteral("管理员");
    default:
        return QStringLiteral("成员");
    }
}

/// 入群时间的显示文本；解析不出（服务端没给 / 0）就留空，别编一个时间
QString joinAtText(const qint64 join_ms)
{
    if (join_ms <= 0)
        return {};

    return QDateTime::fromMSecsSinceEpoch(join_ms).toString(QStringLiteral("yyyy-MM-dd"));
}
} // namespace

GroupController::GroupController(QObject* parent)
    : QObject(parent)
{
    auto& socket = mychat::ChatSocket::instance();

    connect(&socket, &mychat::ChatSocket::groupCreated,
            this, &GroupController::onGroupCreated);
    connect(&socket, &mychat::ChatSocket::groupMembersAdded,
            this, &GroupController::onGroupMembersAdded);
    connect(&socket, &mychat::ChatSocket::groupMemberKicked,
            this, &GroupController::onGroupMemberKicked);
    connect(&socket, &mychat::ChatSocket::groupDismissed,
            this, &GroupController::onGroupDismissed);
    connect(&socket, &mychat::ChatSocket::groupMembersReceived,
            this, &GroupController::onGroupMembersReceived);
    connect(&socket, &mychat::ChatSocket::groupInfoReceived,
            this, &GroupController::onGroupInfoReceived);
    connect(&socket, &mychat::ChatSocket::groupInfoChangeResult,
            this, &GroupController::onGroupInfoChangeResult);

    /*
     * 自己的 uid 从登录回包里拿：SessionManager 由 LoginController 更新，
     * 而自检程序 / 任何不经 LoginController 的调用路径里它是 0 ——
     * 那样成员列表里谁都认不出"我"，myRole 永远是 -1，踢人按钮全灭。
     *
     * ⚠ 只有**换账号**才清列表。首次登录时 m_self_uid 还是空，此时不能清：
     *   QML 页面在 TCP 登录回包之前就已经建好并可能发出 1041 了，
     *   跟着清会把"正在拉取"的状态抹掉 → 同一拍里再发一次，白跑一趟 MySQL。
     */
    connect(&socket, &mychat::ChatSocket::loginSucceeded, this,
            [this](const std::uint64_t uid, const QString&, const QString&)
            {
                const QString next = QString::number(uid);
                if (!m_self_uid.isEmpty() && next != m_self_uid)
                    clearMembers();
                m_self_uid = next;
            });
}

void GroupController::createGroup(const QString& name, const QVariantList& memberUids)
{
    if (m_busy)
    {
        qDebug() << "[group] 上一个群操作还没结束，本次建群被忽略";
        return;
    }

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
    {
        setTip(QStringLiteral("群名称不能为空"), true);
        return;
    }

    if (trimmed.size() > mychat::protocol::MAX_GROUP_NAME_LEN)
    {
        setTip(QStringLiteral("群名称最多 %1 个字")
                   .arg(mychat::protocol::MAX_GROUP_NAME_LEN), true);
        return;
    }

    // QML 传过来的可能是字符串数组，也可能是数字 —— 统一成规范化的十进制串
    QStringList members;
    members.reserve(memberUids.size());
    for (const QVariant& entry: memberUids)
    {
        const QString uid = entry.toString().trimmed();
        if (!uid.isEmpty() && !members.contains(uid))
            members.append(uid);
    }

    setBusy(true);
    setTip(QString());

    /*
     * 头像 / 公告这里都传空：服务端没有上传通道（`avator` 只是个 URL 字段，
     * 传了也没地方取图），界面上也不该摆一个点不动的"上传头像"。
     */
    if (!mychat::ChatSocket::instance().createGroup(trimmed, QString(), QString()))
    {
        setBusy(false);
        setTip(QStringLiteral("创建群聊失败：未连接到聊天服务器"), true);
        return;
    }

    m_pending_members = members;
}

void GroupController::addMembers(const QString& gid, const QVariantList& memberUids)
{
    if (m_busy)
    {
        qDebug() << "[group] 上一个群操作还没结束，本次拉人被忽略";
        return;
    }

    QStringList members;
    members.reserve(memberUids.size());
    for (const QVariant& entry: memberUids)
    {
        const QString uid = entry.toString().trimmed();
        if (!uid.isEmpty() && !members.contains(uid))
            members.append(uid);
    }

    if (members.isEmpty())
    {
        setTip(QStringLiteral("请先选择要拉进群的好友"), true);
        return;
    }

    setBusy(true);
    setTip(QString());

    if (!mychat::ChatSocket::instance().addGroupMembers(gid, members))
    {
        setBusy(false);
        setTip(QStringLiteral("拉人失败：未连接到聊天服务器"), true);
    }
}

void GroupController::kickMember(const QString& gid, const QString& uid)
{
    if (m_busy)
    {
        qDebug() << "[group] 上一个群操作还没结束，本次踢人被忽略";
        return;
    }

    if (gid.isEmpty() || uid.isEmpty())
    {
        setTip(QStringLiteral("踢人参数不完整"), true);
        return;
    }

    setBusy(true);
    setTip(QString());

    if (!mychat::ChatSocket::instance().kickGroupMember(gid, uid))
    {
        setBusy(false);
        setTip(QStringLiteral("操作失败：未连接到聊天服务器"), true);
    }
}

void GroupController::dismissGroup(const QString& gid)
{
    if (m_busy)
    {
        qDebug() << "[group] 上一个群操作还没结束，本次解散被忽略";
        return;
    }

    if (gid.isEmpty())
    {
        setTip(QStringLiteral("解散失败：缺少群号"), true);
        return;
    }

    setBusy(true);
    setTip(QString());

    if (!mychat::ChatSocket::instance().dismissGroup(gid))
    {
        setBusy(false);
        setTip(QStringLiteral("解散失败：未连接到聊天服务器"), true);
    }
}

void GroupController::loadMembers(const QString& gid, const bool force)
{
    if (gid.isEmpty())
        return;

    /*
     * 同一个群、列表已经在手上（或正在拉），就不重复请求 ——
     * 面板每次打开都拉一遍会让服务端多跑一次 MySQL。
     * 例外是 force：其他客户端拉人 / 踢人时服务端不推送成员变化，
     * 只有一条系统消息（from_uid = 1）；页面收到后必须强制重拉一次。
     * 正在拉的时候仍然跳过 —— 那一包请求已经在路上了。
     */
    if (gid == m_gid && m_members_loading)
    {
        qDebug() << "[group] loadMembers 正在拉取，跳过:" << gid;
        return;
    }

    if (!force && gid == m_gid && !m_member_list.isEmpty())
    {
        qDebug() << "[group] loadMembers 命中缓存，跳过:" << gid
            << "size =" << m_member_list.size();
        return;
    }

    qDebug() << "[group] loadMembers 发请求:" << gid << "上次 gid =" << m_gid
        << "loading =" << m_members_loading << "size =" << m_member_list.size();

    m_gid = gid;
    setTip(QString());

    if (!mychat::ChatSocket::instance().fetchGroupMembers(gid))
    {
        /*
         * 两种失败原因要分开说：
         *   · 没登录 / 没连上 —— 用户该知道；
         *   · 另一条群请求正在飞（建群刚回、正在拉人 / 踢人）—— 这是常见的时序，
         *     过一会儿自己会好，弹一句"获取群成员失败"只会误导人。
         */
        if (!mychat::ChatSocket::instance().isLoggedIn())
        {
            setTip(QStringLiteral("获取群成员失败：未连接到聊天服务器"), true);
            return;
        }

        qDebug() << "[group] 另一条群请求还没回包，500ms 后重试拉成员:" << gid;
        QTimer::singleShot(500, this, [this, gid, force]()
                           {
                               if (!m_members_loading && (force || m_member_list.isEmpty()))
                                   loadMembers(gid, force);
                           });
        return;
    }

    m_members_loading = true;
}

void GroupController::loadGroupInfo(const QString& gid, const bool force)
{
    if (gid.isEmpty())
        return;

    /*
     * 和 loadMembers 一样：同群、资料已到手（或正在拉）就不重复请求。
     * 服务端没有群资料变更推送（api-group.md §13），所以除了打开面板的
     * 首次加载，保存成功（1046）后也要 force 一次把新值拿回来。
     */
    if (gid == m_info_gid && m_info_loading)
    {
        qDebug() << "[group] loadGroupInfo 正在拉取，跳过:" << gid;
        return;
    }

    if (!force && gid == m_info_gid && !m_group_info.isEmpty())
    {
        qDebug() << "[group] loadGroupInfo 命中缓存，跳过:" << gid;
        return;
    }

    /*
     * 换群先把旧资料清掉：面板短暂空着没关系，把 A 群的公告摆进 B 群才是事故。
     */
    if (gid != m_info_gid)
    {
        m_group_info.clear();
        emit groupInfoChanged();
    }

    m_info_gid = gid;
    setTip(QString());

    if (!mychat::ChatSocket::instance().getGroupInfo(gid))
    {
        if (!mychat::ChatSocket::instance().isLoggedIn())
        {
            setTip(QStringLiteral("获取群资料失败：未连接到聊天服务器"), true);
            return;
        }

        qDebug() << "[group] 另一条群请求还没回包，500ms 后重试拉资料:" << gid;
        QTimer::singleShot(500, this, [this, gid, force]()
                           {
                               if (!m_info_loading && (force || m_group_info.isEmpty()))
                                   loadGroupInfo(gid, force);
                           });
        return;
    }

    if (!m_info_loading)
    {
        m_info_loading = true;
        emit infoLoadingChanged();
    }
}

void GroupController::changeGroupInfo(const QString& gid, const QString& name,
                                      const QString& announcement)
{
    if (m_busy)
    {
        qDebug() << "[group] 上一个群操作还没结束，本次修改群资料被忽略";
        return;
    }

    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty())
    {
        setTip(QStringLiteral("群名称不能为空"), true);
        return;
    }

    if (trimmed.size() > mychat::protocol::MAX_GROUP_NAME_LEN)
    {
        setTip(QStringLiteral("群名称最多 %1 个字")
                   .arg(mychat::protocol::MAX_GROUP_NAME_LEN), true);
        return;
    }

    /*
     * 公告保留用户输入的换行 / 空格，不做 trim：服务端约定「空串 = 清空公告」
     * （1045 里其它字段缺省/null = 不改），trim 掉首尾空白反而会把用户
     * 特意留的空行吃掉。
     */
    const QString next_announcement = announcement;

    const QVariantMap info = m_group_info;
    if (info.value(QStringLiteral("loaded")).toBool()
        && info.value(QStringLiteral("name")).toString() == trimmed
        && info.value(QStringLiteral("announcement")).toString() == next_announcement)
    {
        setTip(QStringLiteral("群资料没有变化"));
        return;
    }

    setBusy(true);
    setTip(QString());

    m_pending_name = trimmed;
    m_pending_announcement = next_announcement;

    if (!mychat::ChatSocket::instance().changeGroupInfo(
            gid, trimmed, std::nullopt, next_announcement))
    {
        m_pending_name.clear();
        m_pending_announcement.clear();
        setBusy(false);
        setTip(QStringLiteral("修改群资料失败：未连接到聊天服务器"), true);
    }
}

void GroupController::clearMembers()
{
    m_members_loading = false;

    const bool had_members = !(m_member_list.isEmpty() && m_gid.isEmpty() && m_my_role < 0);
    const bool had_info = !(m_group_info.isEmpty() && m_info_gid.isEmpty() && !m_info_loading);

    m_member_list.clear();
    m_gid.clear();
    m_my_role = -1;

    m_group_info.clear();
    m_info_gid.clear();
    if (m_info_loading)
    {
        m_info_loading = false;
        emit infoLoadingChanged();
    }

    if (had_members)
        emit memberListChanged();
    if (had_info)
        emit groupInfoChanged();
}

void GroupController::syncFriends(const QVariantList& friends)
{
    m_friend_names.clear();
    for (const QVariant& entry: friends)
    {
        const QVariantMap friend_item = entry.toMap();
        const QString uid = friend_item.value(QStringLiteral("uid")).toString();
        if (uid.isEmpty())
            continue;

        /*
         * displayName 已经是"备注优先"（ContactController 算好的），直接用；
         * 退一步用 name；都没有就留空，显示时再退到 uid。
         */
        const QString display = friend_item.value(QStringLiteral("displayName")).toString();
        const QString name = friend_item.value(QStringLiteral("name")).toString();
        m_friend_names.insert(uid, display.isEmpty() ? name : display);
    }

    /*
     * 名字变了，列表里的 displayName 也得重算 —— 群昵称为空的那几个人
     * 就是靠好友备注显示的。不重建的话改了备注，群成员面板还挂着旧名字。
     */
    if (m_member_list.isEmpty())
        return;

    QVariantList next;
    next.reserve(m_member_list.size());
    for (const QVariant& entry: m_member_list)
        next.append(entry.toMap()); // 只刷新名字：下面统一重写 displayName
    for (int i = 0; i < next.size(); ++i)
    {
        QVariantMap item = next[i].toMap();
        const QString uid = item.value(QStringLiteral("uid")).toString();
        const QString nick = item.value(QStringLiteral("nickG")).toString();
        const QString friend_name = m_friend_names.value(uid);
        item.insert(QStringLiteral("displayName"),
                    !nick.isEmpty() ? nick : (!friend_name.isEmpty() ? friend_name : uid));
        next[i] = item;
    }
    m_member_list = next;
    emit memberListChanged();
}

QVariantMap GroupController::makeMemberItem(const mychat::protocol::GroupMember& member) const
{
    const QString uid = QString::number(member.uid);
    const QString friend_name = m_friend_names.value(uid);

    QVariantMap item;
    item.insert(QStringLiteral("uid"), uid);
    item.insert(QStringLiteral("nickG"), member.nickG);
    item.insert(QStringLiteral("role"), member.role);
    item.insert(QStringLiteral("roleText"), roleText(member.role));
    item.insert(QStringLiteral("joinAt"), member.joinAtMs);
    item.insert(QStringLiteral("joinAtText"), joinAtText(member.joinAtMs));
    // 群昵称优先；没有就用好友备注 / 昵称；再没有就显示 uid
    item.insert(QStringLiteral("displayName"),
                !member.nickG.isEmpty()
                    ? member.nickG
                    : (!friend_name.isEmpty() ? friend_name : uid));
    item.insert(QStringLiteral("isOwner"), member.role == 2);
    item.insert(QStringLiteral("isAdmin"), member.role == 1);
    item.insert(QStringLiteral("isMe"), uid == m_self_uid);

    /*
     * 能不能踢：服务端的规则是"群主 / 管理员能踢，不能踢群主，管理员之间不能互踢"
     * （api-group.md §6.1）。这里算的是**按钮要不要亮**，真正的裁决仍在存储过程 ——
     * 算宽了会多一次 1024，算窄了用户点不到，所以按服务端规则一比一复刻。
     */
    bool can_kick = false;
    if (!item.value(QStringLiteral("isMe")).toBool() && member.role != 2)
    {
        if (m_my_role == 2)
            can_kick = true;
        else if (m_my_role == 1)
            can_kick = (member.role == 0);
    }
    item.insert(QStringLiteral("canKick"), can_kick);

    return item;
}

void GroupController::refreshMyRole()
{
    m_my_role = -1;

    for (const QVariant& entry: m_member_list)
    {
        const QVariantMap item = entry.toMap();
        if (item.value(QStringLiteral("uid")).toString() == m_self_uid)
        {
            m_my_role = item.value(QStringLiteral("role")).toInt();
            return;
        }
    }
}

void GroupController::setTip(const QString& text, const bool is_error)
{
    if (m_tip == text && m_tip_is_error == is_error)
        return;

    m_tip = text;
    m_tip_is_error = is_error;
    emit tipChanged();
    emit tipIsErrorChanged();
}

void GroupController::setBusy(const bool busy)
{
    if (m_busy == busy)
        return;

    m_busy = busy;
    emit busyChanged();
}

void GroupController::onGroupCreated(const bool ok, const int error, const QString& gid,
                                     const QString& name)
{
    if (!ok)
    {
        m_pending_members.clear();
        setBusy(false);
        setTip(QStringLiteral("创建群聊失败：%1").arg(mychat::protocol::errorToTip(error)), true);
        return;
    }

    qInfo() << "[group] 群聊已创建, gid =" << gid << "name =" << name;

    /*
     * 把新群记成"当前关心的群"：后面拉人成功（onGroupMembersAdded）要按这个 gid
     * 重新拉成员列表。不记的话 `m_gid == gid` 永远不成立，创建完群成员面板一片空白。
     */
    m_gid = gid;
    m_member_list.clear();

    // 先通知外面把会话建出来：即使后面拉人失败，群本身已经存在了
    emit groupCreated(true, error, gid, name);

    const QStringList members = m_pending_members;
    m_pending_members.clear();

    if (members.isEmpty())
    {
        setBusy(false);
        setTip(QStringLiteral("群聊已创建"));
        // 成员列表里至少有自己；不等用户点开面板再拉
        loadMembers(gid);
        return;
    }

    /*
     * 建群和拉人是两次请求，第二步失败时群已经建好了 —— 提示必须说清楚，
     * 否则用户会以为"点了一次没反应"，然后再点一次建出一个空群。
     */
    if (!mychat::ChatSocket::instance().addGroupMembers(gid, members))
    {
        setBusy(false);
        setTip(QStringLiteral("群聊已创建，但拉人失败：请稍后重试"), true);
        loadMembers(gid);
        return;
    }

    // busy 继续保持：等 1036 回来再放行
    setTip(QStringLiteral("群聊已创建，正在拉人…"));
}

void GroupController::onGroupMembersAdded(const bool ok, const int error, const QString& gid,
                                          const QStringList& uids)
{
    if (!ok)
    {
        setBusy(false);
        setTip(QStringLiteral("拉人失败：%1").arg(mychat::protocol::errorToTip(error)), true);
        return;
    }

    qInfo() << "[group] 拉人成功, gid =" << gid << "提交" << uids.size() << "人";

    // 回包不带实际入群名单（已在群里的会被跳过），只能重新拉一次确认
    setTip(QStringLiteral("已提交 %1 位好友入群").arg(uids.size()));

    if (m_gid == gid)
    {
        // 强制重拉：把 m_gid 清掉，绕过 loadMembers 的"同群不重复请求"短路
        m_gid.clear();
        loadMembers(gid);
    }

    emit groupMembersAdded(true, error, gid, uids);
    setBusy(false); // ⚠ 必须最后：订阅方会在 busyChanged 里接着做下一步
}

void GroupController::onGroupMemberKicked(const bool ok, const int error, const QString& gid,
                                          const QString& uid)
{
    if (!ok)
    {
        setBusy(false);
        setTip(QStringLiteral("踢人失败：%1").arg(mychat::protocol::errorToTip(error)), true);
        return;
    }

    qInfo() << "[group] 已踢出成员, gid =" << gid << "uid =" << uid;
    setTip(QStringLiteral("已移出群聊"));

    if (m_gid == gid)
    {
        m_gid.clear();
        loadMembers(gid);
    }

    emit groupMemberKicked(true, error, gid, uid);
    setBusy(false);
}

void GroupController::onGroupDismissed(const bool ok, const int error, const QString& gid)
{
    if (!ok)
    {
        setBusy(false);

        /*
         * ⚠ 服务端群不存在时返回 1013（不是 1023，见 api-group.md §7.3）。
         * 这种情况下"群没了"其实是我们想要的结果，但客户端分不清是
         * 数据库挂了还是群真没了 —— 只能按错误提示，让用户刷新看看。
         */
        setTip(QStringLiteral("解散失败：%1").arg(mychat::protocol::errorToTip(error)), true);
        return;
    }

    qInfo() << "[group] 群聊已解散, gid =" << gid;

    if (m_gid == gid)
        clearMembers();

    emit groupDismissed(true, error, gid);
    setTip(QStringLiteral("群聊已解散"));
    setBusy(false);
}

void GroupController::onGroupMembersReceived(
    const bool ok, const int error, const QString& gid,
    const QList<mychat::protocol::GroupMember>& members)
{
    m_members_loading = false;

    if (!ok)
    {
        setTip(QStringLiteral("获取群成员失败：%1").arg(mychat::protocol::errorToTip(error)), true);
        return;
    }

    if (gid != m_gid)
    {
        // 迟到的另一份回包（切了面板）：丢弃，别把 A 群的成员摆进 B 群
        qDebug() << "[group] 忽略过期群成员回包, gid =" << gid << "当前 =" << m_gid;
        return;
    }

    QVariantList next;
    next.reserve(members.size());
    for (const mychat::protocol::GroupMember& member: members)
        next.append(makeMemberItem(member));

    m_member_list = next;
    refreshMyRole();

    /*
     * 角色变了 → canKick 得重算一遍。makeMemberItem 里用的是写列表之前的 m_my_role，
     * 所以这里按新角色再刷新一次（成员数不多，重算很便宜）。
     */
    for (int i = 0; i < m_member_list.size(); ++i)
    {
        QVariantMap item = m_member_list[i].toMap();
        if (item.value(QStringLiteral("isMe")).toBool())
            continue; // 自己的按钮本来就不显示

        bool can_kick = false;
        const int role = item.value(QStringLiteral("role")).toInt();
        if (role != 2)
        {
            if (m_my_role == 2)
                can_kick = true;
            else if (m_my_role == 1)
                can_kick = (role == 0);
        }
        item.insert(QStringLiteral("canKick"), can_kick);
        m_member_list[i] = item;
    }

    emit memberListChanged();
    qInfo() << "[group] 群成员已更新, gid =" << gid << "人数 =" << m_member_list.size()
        << "我的角色 =" << m_my_role;
}

void GroupController::onGroupInfoReceived(const bool ok, const int error,
                                          const mychat::protocol::GroupInfo& info)
{
    /*
     * 先判过期再清 loading：切面板时旧请求的回包会晚到，清掉 loading 会让
     * 新群的 loading 提前消失（界面闪一下"没有资料"）。
     */
    if (info.gid != m_info_gid)
    {
        // 迟到的另一份回包（切了面板）：丢弃，别把 A 群的资料摆进 B 群
        qDebug() << "[group] 忽略过期群资料回包, gid =" << info.gid
            << "当前 =" << m_info_gid;
        return;
    }

    if (m_info_loading)
    {
        m_info_loading = false;
        emit infoLoadingChanged();
    }

    if (!ok)
    {
        setTip(QStringLiteral("获取群资料失败：%1")
                   .arg(mychat::protocol::errorToTip(error)), true);
        return;
    }

    m_group_info = makeGroupInfoItem(info);
    emit groupInfoChanged();
    qInfo() << "[group] 群资料已更新, gid =" << info.gid << "name =" << info.name
        << "人数 =" << info.memberCount << "/" << info.maxMembers;
}

void GroupController::onGroupInfoChangeResult(const bool ok, const int error, const QString& gid)
{
    if (!ok)
    {
        m_pending_name.clear();
        m_pending_announcement.clear();
        setBusy(false);
        setTip(QStringLiteral("修改群资料失败：%1")
                   .arg(mychat::protocol::errorToTip(error)), true);
        return;
    }

    const QString name = m_pending_name;
    m_pending_name.clear();
    m_pending_announcement.clear();

    qInfo() << "[group] 群资料已修改, gid =" << gid << "name =" << name;

    /*
     * 1046 只回 error，不带新资料；名称 / 公告 / 群主 / 人数都要再拉一次。
     * 这里必须 force：否则 loadGroupInfo 会命中旧缓存，界面显示的还是改之前的值。
     */
    loadGroupInfo(gid, true);

    // 先通知页面改本地会话名（ChatStore.groups 缓存），再放行 busy
    emit groupInfoSaved(gid, name);
    setTip(QStringLiteral("群资料已更新"));
    setBusy(false);
}

QVariantMap GroupController::makeGroupInfoItem(const mychat::protocol::GroupInfo& info) const
{
    QVariantMap item;
    item.insert(QStringLiteral("gid"), info.gid);
    item.insert(QStringLiteral("name"), info.name);
    item.insert(QStringLiteral("avatar"), info.avatar);
    item.insert(QStringLiteral("ownerUid"), info.ownerUid);
    item.insert(QStringLiteral("announcement"), info.announcement);
    item.insert(QStringLiteral("maxMembers"), info.maxMembers);
    item.insert(QStringLiteral("memberCount"), info.memberCount);
    item.insert(QStringLiteral("createAt"), info.createdAtMs);
    item.insert(QStringLiteral("loaded"), info.valid);
    return item;
}
