#include "ContactController.h"

#include <QJsonObject>
#include <QSettings>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "AccountValidator.h"
#include "ChatSocket.h"
#include "SessionManager.h"
#include "logger.h"
#include "protocol.h"

namespace
{
using mychat::protocol::ErrorCode;
constexpr int kSuccess = static_cast<int>(ErrorCode::Success);

/**
 * 好友申请状态 → 可读文案。
 *
 * 取值来自服务端 `FriendApplyStatus`（GateServer/src/public/data/UserInfo.h）：
 *   WAITING=0 / ACCEPTED=1 / REFUSEED=2
 * 放在 C++ 是因为它是**协议侧枚举**，QML 不该知道这些数字的含义 ——
 * 而"state 语义未文档化"的旧注释已经不成立了，源码里写得很清楚。
 */
QString applyStateText(const int state)
{
    switch (state)
    {
        case 0:
            return QStringLiteral("等待处理");
        case 1:
            return QStringLiteral("已接受");
        case 2:
            return QStringLiteral("已拒绝");
        default:
            return QStringLiteral("未知状态 %1").arg(state);
    }
}
} // namespace

ContactController::ContactController(QObject* parent)
    : QObject(parent)
{
    auto& socket = mychat::ChatSocket::instance();

    connect(&socket, &mychat::ChatSocket::searchUserResult,
            this, &ContactController::onSearchResult);
    connect(&socket, &mychat::ChatSocket::friendRequestResult,
            this, &ContactController::onFriendRequestResult);
    connect(&socket, &mychat::ChatSocket::friendRequestList,
            this, &ContactController::onFriendRequestList);
    connect(&socket, &mychat::ChatSocket::friendRequestNotified,
            this, &ContactController::onFriendRequestNotified);
    connect(&socket, &mychat::ChatSocket::authFriendRequestResult,
            this, &ContactController::onAuthFriendResult);
    connect(&socket, &mychat::ChatSocket::friendRequestHandled,
            this, &ContactController::onFriendRequestHandled);
    connect(&socket, &mychat::ChatSocket::friendListResult,
            this, &ContactController::onFriendList);
    connect(&socket, &mychat::ChatSocket::friendDeleteResult,
            this, &ContactController::onFriendDeleteResult);

    // 申请列表一变就重算红点：新申请进来会亮，接受/拒绝后又自己灭掉
    connect(this, &ContactController::applyListChanged,
            this, &ContactController::refreshUnreadApplyCount);
}

std::uint64_t ContactController::selfUid() const
{
    return mychat::SessionManager::instance().uid();
}

void ContactController::ensureSeenLoaded()
{
    const QString uid = QString::number(selfUid());
    if (uid == m_seen_uid)
        return;

    // 换账号（或刚退出到登录页）→ 水位重读；不同账号之间不共享"看过了"
    m_seen_uid = uid;
    m_seen_apply_id = 0;
    if (selfUid() == 0)
        return;

    QSettings settings;
    m_seen_apply_id = settings.value(QStringLiteral("applySeen/") + uid, 0).toLongLong();
}

void ContactController::refreshUnreadApplyCount()
{
    ensureSeenLoaded();

    int count = 0;
    for (const QVariant& entry: m_apply_list)
    {
        const QVariantMap item = entry.toMap();
        // 只数别人发给我的、还在等处理的
        if (!item.value(QStringLiteral("received")).toBool())
            continue;

        bool state_ok = false;
        const int state = item.value(QStringLiteral("state")).toInt(&state_ok);
        if (!state_ok || state != 0) // 0 = 等待处理；接受 / 拒绝过的不再提醒
            continue;

        // 水位之前的看过了
        if (item.value(QStringLiteral("id")).toLongLong() <= m_seen_apply_id)
            continue;

        ++count;
    }

    if (count == m_unread_apply_count)
        return;

    m_unread_apply_count = count;
    emit unreadApplyCountChanged();
}

void ContactController::markApplyListRead()
{
    ensureSeenLoaded();

    qint64 newest = m_seen_apply_id;
    for (const QVariant& entry: m_apply_list)
    {
        const QVariantMap item = entry.toMap();
        if (!item.value(QStringLiteral("received")).toBool())
            continue;

        bool state_ok = false;
        const int state = item.value(QStringLiteral("state")).toInt(&state_ok);
        if (!state_ok || state != 0)
            continue;

        const qint64 id = item.value(QStringLiteral("id")).toLongLong();
        if (id > newest)
            newest = id;
    }

    if (newest > m_seen_apply_id)
    {
        m_seen_apply_id = newest;

        // 落盘：重启后不该把看过一百遍的旧申请又点亮一遍
        if (selfUid() != 0)
        {
            QSettings settings;
            settings.setValue(QStringLiteral("applySeen/") + QString::number(selfUid()),
                              m_seen_apply_id);
        }
    }

    refreshUnreadApplyCount();
}

void ContactController::searchUser(const QString& keyword)
{
    if (m_busy)
        return;

    if (keyword.trimmed().isEmpty())
    {
        setTip(QStringLiteral("请输入要搜索的 uid 或用户名"), true);
        emit validationFailed(QStringLiteral("keyword"));
        return;
    }

    if (!mychat::ChatSocket::instance().isLoggedIn())
    {
        setTip(QStringLiteral("未连接到聊天服务器，请先登录"), true);
        return;
    }

    setBusy(true);
    setTip(QStringLiteral("正在搜索…"));

    if (!mychat::ChatSocket::instance().searchUser(keyword))
    {
        setBusy(false);
        setTip(QStringLiteral("搜索请求发送失败"), true);
    }
}

void ContactController::sendFriendRequest(const QString& toUid, const QString& verifyInfo,
                                          const QString& remark, const QString& peerName)
{
    if (m_busy)
        return;

    // 目标 uid 用同一套规则校验：4 位以上数字、首位非 0
    if (const QString err = AccountValidator::checkUid(toUid); !err.isEmpty())
    {
        setTip(err, true);
        emit validationFailed(QStringLiteral("toUid"));
        return;
    }

    if (const std::uint64_t self = selfUid(); self == 0)
    {
        setTip(QStringLiteral("未登录，无法发送好友申请"), true);
        return;
    }

    if (!mychat::ChatSocket::instance().isLoggedIn())
    {
        setTip(QStringLiteral("未连接到聊天服务器，请先登录"), true);
        return;
    }

    bool be_valid = false;
    const std::uint64_t target = toUid.toULongLong(&be_valid);
    if (!be_valid)
    {
        setTip(QStringLiteral("目标账号超出有效范围"), true);
        emit validationFailed(QStringLiteral("toUid"));
        return;
    }

    setBusy(true);
    setTip(QStringLiteral("正在发送好友申请…"));

    // 记下来：成功回包后要用它补一条本地「已发出」记录（AddFriendRsp 里没有名字）
    m_pending_peer_uid = target;
    m_pending_peer_name = peerName;
    m_pending_verify = verifyInfo;
    m_pending_remark = remark;

    if (!mychat::ChatSocket::instance().sendFriendRequest(selfUid(), target, verifyInfo, remark))
    {
        m_pending_peer_uid = 0;
        m_pending_peer_name.clear();
        m_pending_verify.clear();
        m_pending_remark.clear();
        setBusy(false);
        setTip(QStringLiteral("好友申请发送失败"), true);
    }
}

void ContactController::refreshApplyList()
{
    // 用户 / 页面主动刷新：完事要报"共 N 条"
    m_silent_refresh = false;
    startApplyListRefresh();
}

void ContactController::startApplyListRefresh()
{
    if (m_busy)
    {
        m_silent_refresh = false;
        return;
    }

    if (const std::uint64_t self = selfUid(); self == 0)
    {
        m_silent_refresh = false;
        setTip(QStringLiteral("未登录，无法获取好友申请"), true);
        return;
    }

    if (!mychat::ChatSocket::instance().isLoggedIn())
    {
        m_silent_refresh = false;
        setTip(QStringLiteral("未连接到聊天服务器，请先登录"), true);
        return;
    }

    setBusy(true);
    // 自动刷新（收到推送后补拉）不报"正在刷新" —— 用户这会儿关心的不是这个
    if (!m_silent_refresh)
        setTip(QStringLiteral("正在刷新好友申请…"));

    if (!mychat::ChatSocket::instance().syncFriendRequests(selfUid()))
    {
        m_silent_refresh = false;
        setBusy(false);
        setTip(QStringLiteral("刷新请求发送失败"), true);
    }
}

void ContactController::refreshFriendList()
{
    // 用户 / 页面主动刷新：完事要报一句结果
    m_silent_friend_refresh = false;
    startFriendListRefresh();
}

void ContactController::refreshFriendListSilently()
{
    m_silent_friend_refresh = true;
    startFriendListRefresh();
}

void ContactController::startFriendListRefresh()
{
    if (m_busy)
    {
        /*
         * 忙就排队，别丢 —— 和 refreshApplyList 不同：好友列表的自动刷新是
         * "接受完申请要看到新好友"这种必须发生的事，丢了界面上就少一个人。
         * 由 setBusy(false) 里的 flushPendingFriendRefresh() 兜底补发。
         */
        m_friend_refresh_pending = true;
        return;
    }

    if (selfUid() == 0)
    {
        m_silent_friend_refresh = false;
        m_friend_refresh_pending = false;
        setTip(QStringLiteral("未登录，无法获取好友列表"), true);
        return;
    }

    if (!mychat::ChatSocket::instance().isLoggedIn())
    {
        m_silent_friend_refresh = false;
        m_friend_refresh_pending = false;
        setTip(QStringLiteral("未连接到聊天服务器，请先登录"), true);
        return;
    }

    setBusy(true);
    if (!m_silent_friend_refresh)
        setTip(QStringLiteral("正在获取好友列表…"));

    if (!mychat::ChatSocket::instance().syncFriendList())
    {
        m_silent_friend_refresh = false;
        setBusy(false);
        setTip(QStringLiteral("获取好友列表请求发送失败"), true);
    }
}

bool ContactController::isFriend(const QString& uid) const
{
    const QString target = uid.trimmed();
    if (target.isEmpty())
        return false;

    // 好友列表很小（界面上就几十条），线性扫一遍即可，不必再维护一份 QSet
    for (const QVariant& entry: m_friend_list)
    {
        if (entry.toMap().value(QStringLiteral("uid")).toString() == target)
            return true;
    }
    return false;
}

void ContactController::removeFriend(const QString& uid)
{
    const QString target = uid.trimmed();
    if (target.isEmpty())
    {
        emit validationFailed(QStringLiteral("uid"));
        return;
    }

    if (m_busy)
    {
        // 别静默丢弃：用户点了「删除好友」,界面上必须有反馈
        setTip(QStringLiteral("正在处理上一个请求，请稍后再试"), true);
        return;
    }

    if (selfUid() == 0 || !mychat::ChatSocket::instance().isLoggedIn())
    {
        setTip(QStringLiteral("未连接到聊天服务器，请先登录"), true);
        return;
    }

    // 本地已经不是好友 → 不发请求（服务端会回 1022，白跑一趟）
    if (!isFriend(target))
    {
        setTip(QStringLiteral("你们还不是好友"), true);
        return;
    }

    setBusy(true);
    setTip(QStringLiteral("正在删除好友…"));

    if (!mychat::ChatSocket::instance().delFriend(target))
    {
        // setBusy(false) 放最后：busyChanged 会让订阅方立刻进入下一步
        setTip(QStringLiteral("删除好友请求发送失败"), true);
        setBusy(false);
    }
}

void ContactController::flushPendingFriendRefresh()
{
    if (!m_friend_refresh_pending || m_busy)
        return; // 忙就先不动：setBusy(false) 会再喊一次

    m_friend_refresh_pending = false;
    // 自动触发（刚接受完申请）不覆盖提示 —— 用户这会儿要看的是"已接受好友申请"
    m_silent_friend_refresh = true;
    startFriendListRefresh();
}

void ContactController::handleFriendRequest(const QString& requestId, const int state)
{
    if (m_busy)
        return;

    if (state != 1 && state != 2)
    {
        setTip(QStringLiteral("处理状态只能是「接受」或「拒绝」"), true);
        return;
    }

    bool be_valid = false;
    const std::uint64_t id = requestId.toULongLong(&be_valid);
    if (!be_valid || id == 0)
    {
        setTip(QStringLiteral("好友申请编号无效"), true);
        emit validationFailed(QStringLiteral("requestId"));
        return;
    }

    if (!mychat::ChatSocket::instance().isLoggedIn())
    {
        setTip(QStringLiteral("未连接到聊天服务器，请先登录"), true);
        return;
    }

    setBusy(true);
    setTip(state == 1 ? QStringLiteral("正在接受申请…") : QStringLiteral("正在拒绝申请…"));

    if (!mychat::ChatSocket::instance().authFriendRequest(id, state))
    {
        setBusy(false);
        setTip(QStringLiteral("处理请求发送失败（可能上一条还没回包）"), true);
    }
}

void ContactController::clearSearchResult()
{
    if (m_search_results.isEmpty())
        return;

    m_search_results.clear();
    emit searchResultsChanged();
}

void ContactController::onSearchResult(const bool found, const QList<mychat::protocol::UserInfo>& infos,
                                       const int error)
{
    // ⚠ 同 onFriendRequestList：busyChanged 先于 searchResultsChanged 发出，
    //   在 busyChanged 里读 searchResults 会拿到上一次的结果。
    setBusy(false);

    // 转成 QVariantMap 列表，QML 里用 modelData.xxx 直接取
    QVariantList list;
    list.reserve(infos.size());
    for (const mychat::protocol::UserInfo& info: infos)
    {
        QVariantMap item;
        item.insert(QStringLiteral("uid"), QString::number(info.uid));
        item.insert(QStringLiteral("name"), info.name);
        item.insert(QStringLiteral("desc"), info.desc);
        item.insert(QStringLiteral("sex"), info.sex);
        item.insert(QStringLiteral("icon"), info.icon);
        list.append(item);
    }

    m_search_results = list;
    emit searchResultsChanged();

    if (!found || list.isEmpty())
    {
        // 1014 USER_NOT_FOUND 是正常结果，不该显示成红色错误
        const bool is_not_found = error == static_cast<int>(ErrorCode::UserNotFound);
        QString tip = mychat::protocol::errorToTip(error);
        // 服务端回了空对象/没带 error 时 errorToTip(0) 是空串 —— 红字提示不能是空白，
        // 否则界面上只出现一个"没有内容的错误"，看起来像渲染坏了
        if (tip.isEmpty())
            tip = QStringLiteral("搜索没有结果（服务端未返回数据）");

        setTip(is_not_found ? QStringLiteral("没有找到该用户") : tip, !is_not_found);
        emit searchFinished(false, error);
        return;
    }

    setTip(list.size() == 1
               ? QStringLiteral("已找到用户 %1")
              .arg(list.first().toMap().value(QStringLiteral("name")).toString())
               : QStringLiteral("共找到 %1 位用户，请选择要添加的好友").arg(list.size()));
    emit searchFinished(true, error);
}

void ContactController::appendLocalSentRequest(const std::uint64_t requestId)
{
    if (m_pending_peer_uid == 0)
        return;

    const QString peer_uid = QString::number(m_pending_peer_uid);

    // 已经有"我发给同一个人、还在等处理"的记录就不再补。
    // ⚠ 只认 state == 0：已接受 / 已拒绝的历史记录不算"已经发过" ——
    //   删掉好友后重新发申请是正常路径，那种情况下必须补一条新的，
    //   否则弹窗按 uid 只能找到那条旧记录，显示成"这条申请已经处理过了"。
    for (const QVariant& entry: m_apply_list)
    {
        const QVariantMap item = entry.toMap();
        if (item.value(QStringLiteral("received")).toBool())
            continue;
        if (item.value(QStringLiteral("peerUid")).toString() != peer_uid)
            continue;
        if (item.value(QStringLiteral("state"), -1).toInt() == 0)
            return;
    }

    QVariantMap item;
    item.insert(QStringLiteral("id"), QString::number(requestId));
    item.insert(QStringLiteral("fromUid"), QString::number(selfUid()));
    item.insert(QStringLiteral("toUid"), peer_uid);
    item.insert(QStringLiteral("peerUid"), peer_uid);
    item.insert(QStringLiteral("received"), false);
    item.insert(QStringLiteral("name"), m_pending_peer_name);
    // 我发出的这条，备注就是我刚给对面起的那个名字（服务端刷新回来也是同值）
    item.insert(QStringLiteral("nick"), m_pending_remark);
    item.insert(QStringLiteral("verifyInfo"), m_pending_verify);
    // 本地补的记录没有对方的性别，按"未知"走
    item.insert(QStringLiteral("sex"), 2);
    // 刚发出去就是等待处理
    item.insert(QStringLiteral("state"), 0);
    item.insert(QStringLiteral("stateText"), applyStateText(0));

    // 服务端对「申请人 + 接收人」有唯一键，重发时会**复用原申请编号**（状态重置回待处理）。
    // 所以补记录前先把同编号的旧快照摘掉 —— 否则列表里会出现两条同 id、状态不同的记录。
    QVariantList kept;
    kept.reserve(m_apply_list.size());
    for (const QVariant& entry: m_apply_list)
    {
        const QVariantMap item = entry.toMap();
        if (item.value(QStringLiteral("id")).toString() == QString::number(requestId))
            continue;
        kept.append(item);
    }
    m_apply_list = kept;

    // 服务端按 created_at DESC 排序，新发出的插到最前面，和刷新后的顺序一致
    m_apply_list.prepend(item);
    emit applyListChanged();

    m_pending_peer_uid = 0;
    m_pending_peer_name.clear();
    m_pending_verify.clear();
    m_pending_remark.clear();

    MCLOG_INFO() << "[contact] 已在本地补一条「已发出」申请, to_uid =" << peer_uid;
}

void ContactController::onFriendRequestResult(const bool ok, const int error,
                                              const std::uint64_t requestId)
{
    setBusy(false);

    if (!ok)
    {
        m_pending_peer_uid = 0;
        m_pending_peer_name.clear();
        m_pending_verify.clear();
        m_pending_remark.clear();

        setTip(mychat::protocol::errorToTip(error), true);
        emit friendRequestSent(false, error);
        return;
    }

    // 成功就立刻落一条本地记录：界面上马上能看到，弹窗也能据此切到「申请详情」、
    // 不再给「加好友」按钮 —— 这才是"防止重复发送"的真正闸门
    appendLocalSentRequest(requestId);

    MCLOG_INFO() << "[contact] 好友申请已发送, request id =" << requestId;
    setTip(QStringLiteral("好友申请已发送"));
    emit friendRequestSent(true, error);
}

void ContactController::onFriendRequestList(const QList<mychat::protocol::FriendRequest>& requests,
                                            const int error)
{
    /*
     * ⚠ 这里保持了"先清 busy、后更新列表"的顺序 —— 于是 busyChanged 发出的那一拍，
     *   applyList 还是**旧的**。消费方若在 busyChanged 里读列表，会读到空/过期数据。
     *   （QML 侧的自动化钩子就被这个顺序咬过一次：空列表被误判成"没找到"。）
     *   要根治就把 setBusy(false) 挪到最后 —— 和"先设提示、最后清 busy"的约定一致；
     *   但那会改变信号发射顺序，得连带复核搜索链路与 tests/contactflow.cpp，故暂不动。
     */
    setBusy(false);

    if (error != kSuccess)
    {
        m_silent_refresh = false;
        setTip(mychat::protocol::errorToTip(error), true);
        return;
    }

    const std::uint64_t self = selfUid();

    // 转成 QVariantMap 列表，QML 里用 modelData.xxx 直接取
    QVariantList list;
    list.reserve(requests.size());

    for (const mychat::protocol::FriendRequest& r: requests)
    {
        /*
         * ⚠ 服务端这条查询是「我发出的 + 我收到的」都要：
         *   `WHERE (applicant_uid = ? OR receiver_uid = ?)`（MySqlDao::getFriendRequestListByUid）
         *   而 name / icon / sex 取的是**对方**的（`other_info`）。
         *
         * 所以「对面那个 uid」不能一律用 from_uid —— 我发出的那条 from_uid 就是我本人，
         * QML 拿它去开资料卡会判成"自己"、弹成可编辑的「我的资料」。
         * 这里算好 peerUid（对方）与 received（方向）交给 UI，别让 QML 再猜一遍。
         */
        const bool received = r.fromUid != self;
        const std::uint64_t peer = received ? r.fromUid : r.toUid;

        QVariantMap item;
        item.insert(QStringLiteral("id"), QString::number(r.id));
        item.insert(QStringLiteral("fromUid"), QString::number(r.fromUid));
        item.insert(QStringLiteral("toUid"), QString::number(r.toUid));
        item.insert(QStringLiteral("peerUid"), QString::number(peer));
        item.insert(QStringLiteral("received"), received);
        item.insert(QStringLiteral("name"), r.name);
        // 备注：服务端只给"我发出的那条"填（申请人自己给对面起的名字），
        // 收到的申请这一栏恒为空 —— UI 别把它当"对方给我起的名字"
        item.insert(QStringLiteral("nick"), r.nick);
        item.insert(QStringLiteral("verifyInfo"), r.verifyInfo);
        item.insert(QStringLiteral("sex"), r.sex);
        item.insert(QStringLiteral("state"), r.state);
        // 状态文案在 C++ 里算好，QML 直接显示 —— 两边不要各存一份映射
        item.insert(QStringLiteral("stateText"), applyStateText(r.state));
        list.append(item);
    }

    m_apply_list = list;
    emit applyListChanged();

    // 自动刷新（推送触发的补拉）不覆盖提示，见 startApplyListRefresh 里的说明
    const bool silent = m_silent_refresh;
    m_silent_refresh = false;

    if (!silent)
        setTip(list.isEmpty()
                   ? QStringLiteral("暂无好友申请")
                   : QStringLiteral("共 %1 条好友申请").arg(list.size()));
}

void ContactController::onFriendList(const QList<mychat::protocol::FriendInfo>& friends,
                                     const int error)
{
    if (error != kSuccess)
    {
        m_silent_friend_refresh = false;
        setTip(mychat::protocol::errorToTip(error), true);
        emit friendListRefreshed(false, error);
        // 按约定放最后：busyChanged 会让订阅方立刻进入下一步
        setBusy(false);
        return;
    }

    QVariantList list;
    list.reserve(friends.size());

    for (const mychat::protocol::FriendInfo& f: friends)
    {
        QVariantMap item;
        item.insert(QStringLiteral("uid"), QString::number(f.uid));
        item.insert(QStringLiteral("name"), f.name);
        item.insert(QStringLiteral("desc"), f.desc);
        // nick = 我给这个好友设置的备注（服务端 user_friend.remark）
        item.insert(QStringLiteral("nick"), f.nick);
        // 显示名：备注优先，其次昵称。放在 C++ 里算，QML 各处直接用
        item.insert(QStringLiteral("displayName"), f.nick.isEmpty() ? f.name : f.nick);
        item.insert(QStringLiteral("sex"), f.sex);
        item.insert(QStringLiteral("icon"), f.icon);
        list.append(item);
    }

    m_friend_list = list;
    emit friendListChanged();

    const bool silent = m_silent_friend_refresh;
    m_silent_friend_refresh = false;

    if (!silent)
        setTip(list.isEmpty()
                   ? QStringLiteral("你还没有好友，去「联系人」里搜索并添加吧")
                   : QStringLiteral("共 %1 位好友").arg(list.size()));

    MCLOG_INFO() << "[contact] 好友列表已更新, 共" << list.size() << "位";
    emit friendListRefreshed(true, kSuccess);

    setBusy(false);
}

void ContactController::onFriendDeleteResult(const bool ok, const int error, const QString& uid)
{
    if (!ok)
    {
        setTip(mychat::protocol::errorToTip(error), true);
        emit friendDeleteFinished(false, error, uid);
        // 按约定放最后：busyChanged 会让订阅方立刻进入下一步
        setBusy(false);
        return;
    }

    /*
     * 服务端已确认双向删除 → 本地立刻摘掉，不等下一次 1020。
     *
     * 会话列表由 main.cpp 接的 friendListChanged → ChatController::syncFriends 同步：
     * 有历史记录的会话会保留（本地库里的消息仍然看得见），从没聊过的空壳才会被丢掉。
     */
    QVariantList list;
    list.reserve(m_friend_list.size());
    QString removed_name;
    bool found = false;

    for (const QVariant& entry: m_friend_list)
    {
        const QVariantMap item = entry.toMap();
        if (item.value(QStringLiteral("uid")).toString() == uid)
        {
            removed_name = item.value(QStringLiteral("displayName")).toString();
            found = true;
            continue;
        }
        list.append(item);
    }

    m_friend_list = list;
    emit friendListChanged();

    if (!found)
        qWarning() << "[contact] 删除好友成功，但好友列表里找不到这一条:" << uid;

    setTip(removed_name.isEmpty()
               ? QStringLiteral("已删除好友")
               : QStringLiteral("已删除好友 %1").arg(removed_name));

    MCLOG_INFO() << "[contact] 已删除好友, uid =" << uid << "剩余好友" << list.size() << "位";

    emit friendDeleteFinished(true, kSuccess, uid);

    setBusy(false);
}

void ContactController::onFriendRequestNotified(const std::uint64_t requestId)
{
    MCLOG_INFO() << "[contact] 收到好友申请推送, request id =" << requestId;

    /*
     * 推送只带申请记录 id —— 没有对方 uid / 昵称 / 验证信息，界面要显示详情
     * 只能回拉一次 SyncFriendRequestReq。
     *
     * 但这里**不立刻发**：refreshApplyList() 一遇到 busy 就整条丢弃，而"用户正在
     * 搜索 / 正在发申请"恰恰是常见情形，那样这条推送就白收了。所以挂一个意图，
     * 延到下一拍（也避开在 socket 读回调的调用栈里再发请求），空闲时自动落地。
     */
    m_refresh_pending = true;
    setTip(QStringLiteral("收到新的好友申请"));
    QTimer::singleShot(0, this, &ContactController::flushPendingApplyRefresh);
}

void ContactController::flushPendingApplyRefresh()
{
    if (!m_refresh_pending || m_busy)
        return; // 忙就先不动：setBusy(false) 会再喊一次

    m_refresh_pending = false;
    // 静默刷新：提示留给"收到新的好友申请 / 对方接受了你的申请"这类真正要说的事，
    // 否则用户刚看到结果，一秒后被"共 N 条好友申请"盖掉。
    m_silent_refresh = true;
    startApplyListRefresh();
}

void ContactController::onAuthFriendResult(const bool ok, const int error,
                                           const std::uint64_t requestId, const int state)
{
    const QString id = QString::number(requestId);
    // state 只有 1 / 2 才是确定的；0 = 回包没给、我们也不知道（见 ChatSocket 的说明）
    const bool state_known = (state == 1 || state == 2);

    if (ok && state_known)
        applyRequestState(id, state);

    if (ok)
        setTip(state == 1
                   ? QStringLiteral("已接受好友申请")
                   : state == 2
                   ? QStringLiteral("已拒绝好友申请")
                   : QStringLiteral("好友申请已处理"));
    else
        setTip(mychat::protocol::errorToTip(error), true);

    MCLOG_INFO() << "[contact] 处理好友申请, ok =" << ok << "request id =" << id
        << "state =" << state << "error =" << error;

    emit friendRequestAuthFinished(ok, error, id, state);

    // 结果里没有 state（老服务端 / 认领不到）→ 回拉一次，让列表说出真实状态
    if (ok && !state_known)
    {
        m_refresh_pending = true;
        QTimer::singleShot(0, this, &ContactController::flushPendingApplyRefresh);
    }

    /*
     * 接受成功 = 多了一个好友 → 好友列表必须补刷，否则会话列表里根本没有这个人，
     * 用户会觉得"接受了但没生效"。走挂起意图而不是直接发：此刻控制器还 busy，
     * 由 setBusy(false) 里的 flushPendingFriendRefresh() 落地。
     */
    if (ok && state == 1)
    {
        m_friend_refresh_pending = true;
        QTimer::singleShot(0, this, &ContactController::flushPendingFriendRefresh);
    }

    // 按约定放最后：busyChanged 会让订阅方立刻进入下一步
    setBusy(false);
}

void ContactController::onFriendRequestHandled(const std::uint64_t requestId, const int state)
{
    const QString id = QString::number(requestId);
    applyRequestState(id, state);

    setTip(state == 1
               ? QStringLiteral("对方接受了你的好友申请")
               : QStringLiteral("对方拒绝了你发的好友申请"));

    emit friendRequestAnswered(id, state);

    /*
     * 推送只说"这一条被处理了"，服务端的列表可能还有别的变化（比如对方顺手又发了一条）
     * → 挂一次刷新；控制器正忙时由 setBusy 兜底补发。
     */
    m_refresh_pending = true;
    QTimer::singleShot(0, this, &ContactController::flushPendingApplyRefresh);
}

void ContactController::applyRequestState(const QString& requestId, const int state)
{
    for (int i = 0; i < m_apply_list.size(); ++i)
    {
        QVariantMap item = m_apply_list.at(i).toMap();
        if (item.value(QStringLiteral("id")).toString() != requestId)
            continue;

        item.insert(QStringLiteral("state"), state);
        item.insert(QStringLiteral("stateText"), applyStateText(state));
        m_apply_list[i] = item;
        emit applyListChanged();
        return;
    }
}

void ContactController::setTip(const QString& text, const bool is_error)
{
    if (m_tip == text && m_tip_is_error == is_error)
        return;

    m_tip = text;
    m_tip_is_error = is_error;
    emit tipChanged();
    emit tipIsErrorChanged();
}

void ContactController::setBusy(const bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();

    /*
     * 有挂起的「收到推送 → 回拉申请列表」时，控制器一落地就补发。
     * 延到下一拍是必须的：本文件里好几个 handler 是"先 setBusy(false)、后写数据"
     * （onFriendRequestList / onSearchResult 都这样），此刻同步发请求会和它们
     * 后面还没执行的赋值抢顺序。
     */
    if (!busy && m_refresh_pending)
        QTimer::singleShot(0, this, &ContactController::flushPendingApplyRefresh);

    // 好友列表同理（接受完申请 / 用户连续点两次刷新都会被排到这里）
    if (!busy && m_friend_refresh_pending)
        QTimer::singleShot(0, this, &ContactController::flushPendingFriendRefresh);
}
