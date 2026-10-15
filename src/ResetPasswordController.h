#pragma once

#include <QObject>
#include <QString>

class QJsonObject;
class QTimer;

/**
 * ResetPasswordController —— 重置密码流程控制器
 *
 * 与注册共用「邮箱验证码」这一段流程，但接口各自独立
 * （ResetController 与 RegisterController 的冷却计时器各归各的，
 *   否则两个页面互相干扰）。
 *
 * 流程：
 *   1) requestVerifyCode(email)    → POST /get_Verifycode
 *   2) resetPassword(uid, email, newPassword, verifyCode) → POST /reset_password
 *
 *   · validationFailed(field)  本地校验失败，field 为 uid / email / password / code
 *   · resetSucceeded()         重置成功，通常随后返回登录页
 */
class ResetPasswordController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString tip READ tip NOTIFY tipChanged)
    Q_PROPERTY(bool tipIsError READ tipIsError NOTIFY tipIsErrorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /// 获取验证码的冷却剩余秒数
    Q_PROPERTY(int cooldown READ cooldown NOTIFY cooldownChanged)

public:
    explicit ResetPasswordController(QObject* parent = nullptr);

    [[nodiscard]] QString tip() const { return m_tip; }
    [[nodiscard]] bool tipIsError() const { return m_tip_is_error; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] int cooldown() const { return m_cooldown; }

    Q_INVOKABLE void requestVerifyCode(const QString& email);

    /// uid 传字符串：与登录一致，服务端对 /reset_password 的 user 字段也是按字符串解析的
    Q_INVOKABLE void resetPassword(const QString& uid, const QString& email,
                                   const QString& newPassword, const QString& verifyCode);

signals:
    void tipChanged();
    void tipIsErrorChanged();
    void busyChanged();
    void cooldownChanged();
    void validationFailed(const QString& field);
    void verifyCodeRequested();
    void resetSucceeded();
    /// 用户在重置页点了「返回登录」
    void backToLoginRequested();

private:
    void setTip(const QString& text, bool is_error = false);
    void setBusy(bool busy);
    void startCooldown();
    void onCooldownTick();

    void onVerifyCodeDone(bool ok, const QJsonObject& resp);
    void onResetDone(bool ok, const QJsonObject& resp);

    QString m_tip;
    bool m_tip_is_error = false;
    bool m_busy = false;

    QTimer* m_cooldown_timer = nullptr;
    int m_cooldown = 0;
};
