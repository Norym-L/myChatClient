#include "ContactController.h"

#include <QJsonObject>
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
}

std::uint64_t ContactController::selfUid() const
{
    return mychat::SessionManager::instance().uid();
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

    if (!mychat::ChatSocket::instance().sendFriendRequest(selfUid(), target, verifyInfo, remark))
    {
        m_pending_peer_uid = 0;
        m_pending_peer_name.clear();
        m_pending_verify.clear();
        setBusy(false);
        setTip(QStringLiteral("好友申请发送失败"), true);
    }
}

void ContactController::refreshApplyList()
{
    if (m_busy)
        return;

    if (const std::uint64_t self = selfUid(); self == 0)
    {
        setTip(QStringLiteral("未登录，无法获取好友申请"), true);
        return;
    }

    if (!mychat::ChatSocket::instance().isLoggedIn())
    {
        setTip(QStringLiteral("未连接到聊天服务器，请先登录"), true);
        return;
    }

    setBusy(true);
    setTip(QStringLiteral("正在刷新好友申请…"));

    if (!mychat::ChatSocket::instance().syncFriendRequests(selfUid()))
    {
        setBusy(false);
        setTip(QStringLiteral("刷新请求发送失败"), true);
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
        setTip(is_not_found
                   ? QStringLiteral("没有找到该用户")
                   : mychat::protocol::errorToTip(error),
               !is_not_found);
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

    // 已经有"我发给同一个人"的记录了就不再补。
    // 正常流程走不到这里（弹窗那边一旦有申请就不给发），但控制器是所有入口的必经之处，
    // 兜一层比让列表里出现两条一模一样的更省事。
    for (const QVariant& entry: m_apply_list)
    {
        const QVariantMap item = entry.toMap();
        if (item.value(QStringLiteral("received")).toBool())
            continue;
        if (item.value(QStringLiteral("peerUid")).toString() == peer_uid)
            return;
    }

    QVariantMap item;
    item.insert(QStringLiteral("id"), QString::number(requestId));
    item.insert(QStringLiteral("fromUid"), QString::number(selfUid()));
    item.insert(QStringLiteral("toUid"), peer_uid);
    item.insert(QStringLiteral("peerUid"), peer_uid);
    item.insert(QStringLiteral("received"), false);
    item.insert(QStringLiteral("name"), m_pending_peer_name);
    item.insert(QStringLiteral("verifyInfo"), m_pending_verify);
    // 本地补的记录没有对方的性别，按"未知"走
    item.insert(QStringLiteral("sex"), 2);
    // 刚发出去就是等待处理
    item.insert(QStringLiteral("state"), 0);
    item.insert(QStringLiteral("stateText"), applyStateText(0));

    // 服务端按 created_at DESC 排序，新发出的插到最前面，和刷新后的顺序一致
    m_apply_list.prepend(item);
    emit applyListChanged();

    m_pending_peer_uid = 0;
    m_pending_peer_name.clear();
    m_pending_verify.clear();

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
        item.insert(QStringLiteral("verifyInfo"), r.verifyInfo);
        item.insert(QStringLiteral("sex"), r.sex);
        item.insert(QStringLiteral("state"), r.state);
        // 状态文案在 C++ 里算好，QML 直接显示 —— 两边不要各存一份映射
        item.insert(QStringLiteral("stateText"), applyStateText(r.state));
        list.append(item);
    }

    m_apply_list = list;
    emit applyListChanged();

    setTip(list.isEmpty()
               ? QStringLiteral("暂无好友申请")
               : QStringLiteral("共 %1 条好友申请").arg(list.size()));
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
}
