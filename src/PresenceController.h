#pragma once

#include <QObject>
#include <QSet>
#include <QString>
#include <QVariantMap>

#include <cstdint>

/**
 * PresenceController —— 「谁在线」的查询与缓存（QML 与 core 之间的唯一入口）
 *
 * 服务端 `MESSAGE_CHECK_IS_USER_ONLINE_REQ/RSP` 一次只能问一个人，而且回包**不带 uid**
 * （只有 error + online）。本类负责把「问谁」记下来、把结果按 uid 存好，
 * QML 只管按 uid 读。
 *
 * QML 侧：
 *   PresenceController.refresh(uid)         发起一次查询（同一 uid 不会重复发）
 *   PresenceController.isOnline(uid)        已知且在线
 *   PresenceController.hasState(uid)        是否已经有结果（false = 未知，别当离线显示）
 *   读 PresenceController.online            QVariantMap：uid -> bool，只含已知的
 *
 * 信号：
 *   stateChanged(uid, online)               某个 uid 刚更新
 */
class PresenceController : public QObject
{
    Q_OBJECT

    /// uid（字符串）-> bool（是否在线）。**只存已经查到结果的**：key 不存在＝未知
    Q_PROPERTY(QVariantMap online READ online NOTIFY onlineChanged)

public:
    explicit PresenceController(QObject* parent = nullptr);

    [[nodiscard]] QVariantMap online() const { return m_online; }

    /**
     * 发起一次在线状态查询。
     *
     * \note 未登录时静默忽略 —— 登录链路还没走完，问了也没人回。
     *       同一个 uid 已经有请求在飞时不重复发（回包不带 uid，重复发会分不清谁是谁）。
     */
    Q_INVOKABLE void refresh(const QString& uid);

    /// 是否已经查到过这个 uid 的结果。false 表示**未知**，不是离线
    Q_INVOKABLE bool hasState(const QString& uid) const;

    /// 已知且在线（未知一律返回 false，调用方要区分就用 hasState）
    Q_INVOKABLE bool isOnline(const QString& uid) const;

signals:
    void onlineChanged();
    /// 某个 uid 的在线状态刚更新
    void stateChanged(const QString& uid, bool online);

private:
    void onUserOnlineResult(std::uint64_t uid, bool online, int error);
    /// 换会话 / 断链时把缓存清掉：跨会话的在线状态没有意义
    void clear();

    QVariantMap m_online;
    /// 已发出、还没回包的 uid —— 用来去重，不是缓存
    QSet<QString> m_pending;
};
