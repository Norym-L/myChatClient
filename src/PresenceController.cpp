#include "PresenceController.h"

#include <QDebug>

#include "ChatSocket.h"
#include "protocol.h"

namespace
{
using mychat::protocol::ErrorCode;
constexpr int kSuccess = static_cast<int>(ErrorCode::Success);
} // namespace

PresenceController::PresenceController(QObject* parent)
    : QObject(parent)
{
    auto& socket = mychat::ChatSocket::instance();

    connect(&socket, &mychat::ChatSocket::userOnlineResult,
            this, &PresenceController::onUserOnlineResult);

    // 换会话（重新登录成功）或掉线：缓存的在线状态立刻作废。
    // 不清的话，上一个会话里"在线"的人会在新会话里继续显示成在线。
    connect(&socket, &mychat::ChatSocket::loginSucceeded,
            this, [this]() { clear(); });
    connect(&socket, &mychat::ChatSocket::linkError,
            this, [this](const QString&) { clear(); });
}

void PresenceController::refresh(const QString& uid)
{
    if (uid.isEmpty())
        return;

    if (!mychat::ChatSocket::instance().isLoggedIn())
    {
        // 登录链路还没走完（或已断开），问了也没人回 —— 静默返回，不刷警告
        qDebug() << "[presence] 未登录，跳过在线查询:" << uid;
        return;
    }

    if (m_pending.contains(uid))
        return; // 这一轮已经问过这个人了

    if (mychat::ChatSocket::instance().checkUserOnline(uid))
        m_pending.insert(uid);
}

bool PresenceController::hasState(const QString& uid) const
{
    return m_online.contains(uid);
}

bool PresenceController::isOnline(const QString& uid) const
{
    return m_online.value(uid).toBool();
}

void PresenceController::onUserOnlineResult(const std::uint64_t uid, const bool online,
                                            const int error)
{
    const QString key = QString::number(uid);
    m_pending.remove(key);

    if (error != kSuccess)
    {
        // 查询失败就保持"未知"：把失败当成离线显示是在编数据。
        // （比如 1013 SERVICE_UNAVAILABLE 是 Redis 挂了，那时候谁都不知道谁在线）
        qWarning() << "[presence] 查询在线状态失败, uid =" << key << "error =" << error;
        return;
    }

    if (m_online.contains(key) && m_online.value(key).toBool() == online)
        return; // 没变化，不必惊动界面

    m_online.insert(key, online);
    emit onlineChanged();
    emit stateChanged(key, online);

    qDebug() << "[presence] uid" << key << (online ? "在线" : "离线");
}

void PresenceController::clear()
{
    m_pending.clear();

    if (m_online.isEmpty())
        return;

    m_online.clear();
    emit onlineChanged();
}
