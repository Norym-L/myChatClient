#include "RegisterController.h"

#include <QJsonObject>
#include <QTimer>

#include "AccountValidator.h"
#include "HttpClient.h"
#include "logger.h"
#include "protocol.h"

namespace
{
/// 获取验证码后的冷却秒数（服务端验证码有效期也是这个量级，避免用户反复触发发信）
constexpr int kVerifyCooldownSec = 60;

using mychat::protocol::ErrorCode;
using mychat::protocol::ReqId;

constexpr int kSuccess = static_cast<int>(ErrorCode::Success);
} // namespace

RegisterController::RegisterController(QObject* parent)
    : QObject(parent)
    , m_cooldown_timer(new QTimer(this))
{
    m_cooldown_timer->setInterval(1000);
    connect(m_cooldown_timer, &QTimer::timeout, this, &RegisterController::onCooldownTick);
}

void RegisterController::requestVerifyCode(const QString& email)
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

void RegisterController::registerUser(const QString& email, const QString& nickname,
                                      const QString& password, const QString& confirm,
                                      const QString& verifyCode)
{
    if (m_busy)
        return;

    // ── 1) 本地校验（规则与登录共用一份，见 AccountValidator）──
    if (const QString err = AccountValidator::checkEmail(email); !err.isEmpty())
    {
        setTip(err, true);
        emit validationFailed(QStringLiteral("email"));
        return;
    }
    if (const QString err = AccountValidator::checkNickname(nickname); !err.isEmpty())
    {
        setTip(err, true);
        emit validationFailed(QStringLiteral("nickname"));
        return;
    }
    if (const QString err = AccountValidator::checkPassword(password); !err.isEmpty())
    {
        setTip(err, true);
        emit validationFailed(QStringLiteral("password"));
        return;
    }
    if (confirm != password)
    {
        setTip(QStringLiteral("两次输入的密码不一致"), true);
        emit validationFailed(QStringLiteral("confirm"));
        return;
    }
    if (const QString err = AccountValidator::checkVerifyCode(verifyCode); !err.isEmpty())
    {
        setTip(err, true);
        emit validationFailed(QStringLiteral("code"));
        return;
    }

    // ── 2) POST /user_register ──
    QJsonObject body;
    body.insert(QStringLiteral("email"), email);
    body.insert(QStringLiteral("user"), nickname); // 注意：这里的 user 是**昵称**，不是 uid
    body.insert(QStringLiteral("password"), password);
    body.insert(QStringLiteral("confirm"), confirm);
    body.insert(QStringLiteral("verify_code"), verifyCode);

    setBusy(true);
    setTip(QStringLiteral("正在注册…"));

    mychat::HttpClient::instance().post(
        mychat::protocol::reqPath(ReqId::RegisterUser), body,
        [this](const bool ok, const QJsonObject& resp) { onRegisterDone(ok, resp); });
}

void RegisterController::onVerifyCodeDone(const bool ok, const QJsonObject& resp)
{
    // ⚠ 顺序要紧：**先设好提示，最后再清 busy**。
    // 清 busy 会发 busyChanged，订阅方可能立刻发起下一步（本工程的自检程序就这么干）；
    // 那时提示若还没写好，就会被下一步的提示覆盖，用户看到的是上一步的错误信息。
    const auto finish = [this](const QString& tip, const bool is_error)
        {
            setTip(tip, is_error);
            setBusy(false);
        };

    // 业务失败同样走 HTTP 200，必须再看 body 里的 error。
    // 这个接口的 error 语义与其它接口不同（透传 VerifyServer 的码），用专用映射。
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

    // Debug 服务端会把验证码直接回在响应里（GATE_DEBUG），本地联调省去收邮件。
    // 这里只记日志，不往 QML 暴露 —— 免得 Release 构建下出现"拿不到码"的困惑。
    if (resp.contains(QStringLiteral("verify_code")))
    {
        MCLOG_INFO() << "[register] 服务端回显的验证码:"
            << resp.value(QStringLiteral("verify_code")).toString();
    }

    startCooldown();
    finish(QStringLiteral("验证码已发送，请查收邮件"), false);
    emit verifyCodeRequested();
}

void RegisterController::onRegisterDone(const bool ok, const QJsonObject& resp)
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

    // uid 统一按字符串处理（QJsonValue 内部是 double，>2^53 会丢精度）
    const auto uid = mychat::protocol::parseUid(resp.value(QStringLiteral("uid")));
    const QString uid_text = uid.has_value() ? QString::number(*uid) : QString();

    if (uid_text.isEmpty())
    {
        MCLOG_WARN() << "[register] 注册响应里的 uid 非法:" << resp;
        setTip(QStringLiteral("注册成功，但未取到用户 id，请直接登录"), false);
        setBusy(false);
        emit registerSucceeded(QString());
        return;
    }

    MCLOG_INFO() << "[register] 注册成功, uid =" << uid_text;
    // 把 uid 回显给用户 —— 登录用的是 uid 而不是邮箱，不显示的话用户根本无从登录
    setTip(QStringLiteral("注册成功，你的账号是 %1，请返回登录").arg(uid_text), false);
    setBusy(false);
    emit registerSucceeded(uid_text);
}

void RegisterController::startCooldown()
{
    m_cooldown = kVerifyCooldownSec;
    emit cooldownChanged();
    m_cooldown_timer->start();
}

void RegisterController::onCooldownTick()
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

void RegisterController::setTip(const QString& text, const bool is_error)
{
    if (m_tip == text && m_tip_is_error == is_error)
        return;

    m_tip = text;
    m_tip_is_error = is_error;
    emit tipChanged();
    emit tipIsErrorChanged();
}

void RegisterController::setBusy(const bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}
