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
                                          const QString& remark)
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

    if (!mychat::ChatSocket::instance().sendFriendRequest(selfUid(), target, verifyInfo, remark))
    {
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

void ContactController::onFriendRequestResult(const bool ok, const int error,
                                              const std::uint64_t requestId)
{
    setBusy(false);

    if (!ok)
    {
        setTip(mychat::protocol::errorToTip(error), true);
        emit friendRequestSent(false, error);
        return;
    }

    MCLOG_INFO() << "[contact] 好友申请已发送, request id =" << requestId;
    setTip(QStringLiteral("好友申请已发送"));
    emit friendRequestSent(true, error);
}

void ContactController::onFriendRequestList(const QList<mychat::protocol::FriendRequest>& requests,
                                            const int error)
{
    setBusy(false);

    if (error != kSuccess)
    {
        setTip(mychat::protocol::errorToTip(error), true);
        return;
    }

    // 转成 QVariantMap 列表，QML 里用 modelData.xxx 直接取
    QVariantList list;
    list.reserve(requests.size());

    for (const mychat::protocol::FriendRequest& r: requests)
    {
        QVariantMap item;
        item.insert(QStringLiteral("id"), QString::number(r.id));
        item.insert(QStringLiteral("fromUid"), QString::number(r.fromUid));
        item.insert(QStringLiteral("toUid"), QString::number(r.toUid));
        item.insert(QStringLiteral("name"), r.name);
        item.insert(QStringLiteral("verifyInfo"), r.verifyInfo);
        item.insert(QStringLiteral("sex"), r.sex);
        item.insert(QStringLiteral("state"), r.state);
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
