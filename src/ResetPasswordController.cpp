#include "ResetPasswordController.h"

#include <QJsonObject>
#include <QTimer>

#include "AccountValidator.h"
#include "HttpClient.h"
#include "logger.h"
#include "protocol.h"

namespace
{
constexpr int kVerifyCooldownSec = 60;

using mychat::protocol::ErrorCode;
using mychat::protocol::ReqId;

constexpr int kSuccess = static_cast<int>(ErrorCode::Success);
} // namespace

ResetPasswordController::ResetPasswordController(QObject* parent)
    : QObject(parent)
    , m_cooldown_timer(new QTimer(this))
{
    m_cooldown_timer->setInterval(1000);
    connect(m_cooldown_timer, &QTimer::timeout, this, &ResetPasswordController::onCooldownTick);
}

void ResetPasswordController::requestVerifyCode(const QString& email)
{
    if (m_busy || m_cooldown > 0)
        return;

    if (const QString err = AccountValidator::checkEmail(email); !err.isEmpty())
    {
        setTip(err, true);
        emit validationFailed(QStringLiteral("email"));
        return;
    }

    QJsonObject body;
    body.insert(QStringLiteral("email"), email);

    setBusy(true);
    setTip(QStringLiteral("正在发送验证码…"));

    mychat::HttpClient::instance().post(
        mychat::protocol::reqPath(ReqId::GetVerifyCode), body,
        [this](const bool ok, const QJsonObject& resp) { onVerifyCodeDone(ok, resp); });
}

void ResetPasswordController::resetPassword(const QString& uid, const QString& email,
                                            const QString& newPassword, const QString& verifyCode)
{
    if (m_busy)
        return;

    if (const QString err = AccountValidator::checkUid(uid); !err.isEmpty())
    {
        setTip(err, true);
        emit validationFailed(QStringLiteral("uid"));
        return;
    }
    if (const QString err = AccountValidator::checkEmail(email); !err.isEmpty())
    {
        setTip(err, true);
        emit validationFailed(QStringLiteral("email"));
        return;
    }
    if (const QString err = AccountValidator::checkPassword(newPassword); !err.isEmpty())
    {
        setTip(err, true);
        emit validationFailed(QStringLiteral("password"));
        return;
    }
    if (const QString err = AccountValidator::checkVerifyCode(verifyCode); !err.isEmpty())
    {
        setTip(err, true);
        emit validationFailed(QStringLiteral("code"));
        return;
    }

    QJsonObject body;
    // 服务端对 /reset_password 的 user 字段按**字符串**解析（见 LogicSystem 里
    // "invalid uid string" 那条日志），所以这里照字符串提交，与登录保持一致。
    body.insert(QStringLiteral("user"), uid);
    body.insert(QStringLiteral("email"), email);
    body.insert(QStringLiteral("password"), newPassword);
    body.insert(QStringLiteral("verify_code"), verifyCode);

    setBusy(true);
    setTip(QStringLiteral("正在重置密码…"));

    mychat::HttpClient::instance().post(
        mychat::protocol::reqPath(ReqId::ResetPassword), body,
        [this](const bool ok, const QJsonObject& resp) { onResetDone(ok, resp); });
}

void ResetPasswordController::onVerifyCodeDone(const bool ok, const QJsonObject& resp)
{
    // ⚠ 顺序要紧：**先设好提示，最后再清 busy**（理由同 RegisterController，
    // 清 busy 可能触发订阅方立刻进入下一步，提示会被覆盖）。
    const auto finish = [this](const QString& tip, const bool is_error)
        {
            setTip(tip, is_error);
            setBusy(false);
        };

    // 这个接口的 error 语义与其它接口不同（透传 VerifyServer 的码），用专用映射
    if (!ok)
    {
        finish(mychat::protocol::errorToTip(static_cast<int>(ErrorCode::NetworkError)), true);
        return;
    }

    const int error = resp.value(QStringLiteral("error")).toInt(kSuccess);
    if (error != kSuccess)
    {
        finish(mychat::protocol::verifyCodeErrorToTip(error), true);
        return;
    }

    if (resp.contains(QStringLiteral("verify_code")))
    {
        MCLOG_INFO() << "[reset] 服务端回显的验证码:"
            << resp.value(QStringLiteral("verify_code")).toString();
    }

    startCooldown();
    finish(QStringLiteral("验证码已发送，请查收邮件"), false);
    emit verifyCodeRequested();
}

void ResetPasswordController::onResetDone(const bool ok, const QJsonObject& resp)
{
    if (!ok)
    {
        setTip(mychat::protocol::errorToTip(static_cast<int>(ErrorCode::NetworkError)), true);
        setBusy(false);
        return;
    }

    const int error = resp.value(QStringLiteral("error")).toInt(kSuccess);
    if (error != kSuccess)
    {
        setTip(mychat::protocol::errorToTip(error), true);
        setBusy(false);
        return;
    }

    MCLOG_INFO() << "[reset] 密码重置成功";
    setTip(QStringLiteral("密码已重置，请返回登录"), false);
    setBusy(false);
    emit resetSucceeded();
}

void ResetPasswordController::startCooldown()
{
    m_cooldown = kVerifyCooldownSec;
    emit cooldownChanged();
    m_cooldown_timer->start();
}

void ResetPasswordController::onCooldownTick()
{
    if (m_cooldown <= 0)
    {
        m_cooldown_timer->stop();
        return;
    }

    --m_cooldown;
    emit cooldownChanged();

    if (m_cooldown <= 0)
        m_cooldown_timer->stop();
}

void ResetPasswordController::setTip(const QString& text, const bool is_error)
{
    if (m_tip == text && m_tip_is_error == is_error)
        return;

    m_tip = text;
    m_tip_is_error = is_error;
    emit tipChanged();
    emit tipIsErrorChanged();
}

void ResetPasswordController::setBusy(const bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}
