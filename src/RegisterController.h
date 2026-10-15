#pragma once

#include <QObject>
#include <QString>

class QJsonObject;
class QTimer;

/**
 * RegisterController —— 注册流程控制器（QML 与 core 之间的唯一入口）
 *
 * 流程分两步，与服务端契约一致：
 *   1) requestVerifyCode(email)  → POST /get_Verifycode，验证码以 code_<email> 落到 Redis
 *   2) registerUser(...)         → POST /user_register
 *
 *   · validationFailed(field)    本地校验失败，field 为 email / nickname /
 *                                password / confirm / code
 *   · verifyCodeRequested()      验证码已请求（Debug 服务端会把码回在响应里，见日志）
 *   · registerSucceeded(uid)     注册成功，参数是服务端下发的新 uid
 *
 * 方法名叫 registerUser 而不是 register —— `register` 是 C++ 保留字，不能作函数名。
 */
class RegisterController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString tip READ tip NOTIFY tipChanged)
    /// tip 是否为错误提示（QML 据此决定文字用危险色还是普通色）
    Q_PROPERTY(bool tipIsError READ tipIsError NOTIFY tipIsErrorChanged)
    /// 请求进行中的忙碌态
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /// 获取验证码的冷却剩余秒数；> 0 时不应再允许用户点「获取验证码」
    Q_PROPERTY(int cooldown READ cooldown NOTIFY cooldownChanged)

public:
    explicit RegisterController(QObject* parent = nullptr);

    [[nodiscard]] QString tip() const { return m_tip; }
    [[nodiscard]] bool tipIsError() const { return m_tip_is_error; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] int cooldown() const { return m_cooldown; }

    /// 请求邮箱验证码；成功后进入冷却，避免用户反复触发发信
    Q_INVOKABLE void requestVerifyCode(const QString& email);

    /// 提交注册。校验与网络都在 C++ 内部，QML 只传原始输入。
    Q_INVOKABLE void registerUser(const QString& email, const QString& nickname,
                                  const QString& password, const QString& confirm,
                                  const QString& verifyCode);

    signals:

    void tipChanged();
    void tipIsErrorChanged();
    void busyChanged();
    void cooldownChanged();
    /// 本地校验失败，field 为 email / nickname / password / confirm / code
    void validationFailed(const QString& field);
    /// 验证码已请求
    void verifyCodeRequested();
    /// 注册成功，uid 为服务端下发的新用户 id
    void registerSucceeded(const QString& uid);
    /// 用户在注册页点了「返回登录」
    void backToLoginRequested();

private:
    void setTip(const QString& text, bool is_error = false);
    void setBusy(bool busy);
    void startCooldown();
    void onCooldownTick();

    void onVerifyCodeDone(bool ok, const QJsonObject& resp);
    void onRegisterDone(bool ok, const QJsonObject& resp);

    QString m_tip;
    bool m_tip_is_error = false;
    bool m_busy = false;

    QTimer* m_cooldown_timer = nullptr;
    int m_cooldown = 0;
};
