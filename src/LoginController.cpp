#include "LoginController.h"

#include <QJsonObject>

#include "AccountValidator.h"
#include "ChatSocket.h"
#include "HttpClient.h"
#include "SessionManager.h"
#include "logger.h"
#include "protocol.h"

namespace
{
    using mychat::protocol::ErrorCode;

    constexpr int kSuccess = static_cast<int>(ErrorCode::Success);
} // namespace

LoginController::LoginController(QObject* parent)
    : QObject(parent)
{
    auto& socket = mychat::ChatSocket::instance();

    connect(&socket, &mychat::ChatSocket::connected,
            this, &LoginController::onSocketConnected);
    connect(&socket, &mychat::ChatSocket::connectFailed,
            this, &LoginController::onSocketConnectFailed);
    connect(&socket, &mychat::ChatSocket::loginSucceeded,
            this, &LoginController::onChatLoginSucceeded);
    connect(&socket, &mychat::ChatSocket::loginFailed,
            this, &LoginController::onChatLoginFailed);
    connect(&socket, &mychat::ChatSocket::linkError,
            this, &LoginController::onLinkError);
}

QString LoginController::CheckAccount(const QString& text)
{
    // 规则与注册 / 重置共用同一份实现，避免三处各写一套正则而互相漂移
    return AccountValidator::checkUid(text);
}

QString LoginController::CheckPassword(const QString& text)
{
    return AccountValidator::checkPassword(text);
}

void LoginController::login(const QString& account, const QString& password)
{
    if (m_busy)
        return;

    // ── 1) 本地校验 ──
    QString err = CheckAccount(account);
    if (!err.isEmpty())
    {
        setTip(err, true);
        emit validationFailed(QStringLiteral("account"));
        return;
    }
    err = CheckPassword(password);
    if (!err.isEmpty())
    {
        setTip(err, true);
        emit validationFailed(QStringLiteral("password"));
        return;
    }

    // 约定：/user_login 的 user 字段是 uid，必须以 uint64 的**字符串**形式提交
    //（QJsonValue 内部是 double，超过 2^53 会静默丢精度）。
    bool be_valid_uid = false;
    const std::uint64_t uid = account.toULongLong(&be_valid_uid);
    if (!be_valid_uid)
    {
        setTip(tr("账号超出有效范围"), true);
        emit validationFailed(QStringLiteral("account"));
        return;
    }

    // ── 2) HTTP 登录 ──
    QJsonObject body;
    body.insert(QStringLiteral("user"), QString::number(uid));
    body.insert(QStringLiteral("password"), password);

    setBusy(true);
    setTip(tr("正在登录…"));

    mychat::HttpClient::instance().post(
        mychat::protocol::reqPath(mychat::protocol::ReqId::LoginUser), body,
        [this](const bool ok, const QJsonObject& resp) { onHttpLoginDone(ok, resp); });
}

void LoginController::onHttpLoginDone(const bool ok, const QJsonObject& resp)
{
    if (!ok)
    {
        fail(mychat::protocol::errorToTip(
            resp.value(QStringLiteral("error")).toInt()));
        return;
    }

    // 文档：业务失败同样是 HTTP 200，要再看 body 里的 error
    const int error = resp.value(QStringLiteral("error"))
                          .toInt(static_cast<int>(ErrorCode::ServerJsonError));
    if (error != kSuccess)
    {
        fail(mychat::protocol::errorToTip(error));
        return;
    }

    // uid 由服务端以 uint64 字符串形式下发（用 toInt() 会截断大 uid）
    const std::optional<std::uint64_t> uid =
        mychat::protocol::parseUid(resp.value(QStringLiteral("uid")));
    if (!uid.has_value())
    {
        MCLOG_WARN() << "[login] 登录响应里的 uid 非法:" << resp;
        fail(tr("服务器返回数据不完整"));
        return;
    }

    const QString token = resp.value(QStringLiteral("token")).toString();
    const QString host = resp.value(QStringLiteral("host")).toString();
    const QString port_text = resp.value(QStringLiteral("port")).toString();

    if (token.isEmpty() || host.isEmpty() || port_text.isEmpty())
    {
        MCLOG_WARN() << "[login] 登录响应字段不完整:" << resp;
        fail(tr("服务器返回数据不完整"));
        return;
    }

    bool be_valid_port = false;
    const std::uint16_t port = port_text.toUShort(&be_valid_port);
    if (!be_valid_port || port == 0)
    {
        MCLOG_WARN() << "[login] 聊天服端口非法:" << port_text;
        fail(tr("服务器返回的聊天服端口无效"));
        return;
    }

    m_uid = *uid;
    m_token = token;

    MCLOG_INFO() << "[login] HTTP 登录成功: uid =" << *uid
        << "host =" << host << "port =" << port;

    // ── 3) 连接 ChatServer ──
    setTip(tr("登录成功，正在连接聊天服务器…"));
    mychat::ChatSocket::instance().connectToServer(host, port);
}

void LoginController::onSocketConnected()
{
    setTip(tr("已连接聊天服务器，正在登录…"));

    if (!mychat::ChatSocket::instance().login(m_uid, m_token))
        fail(tr("登录消息发送失败"));
}

void LoginController::onSocketConnectFailed(const QString& reason)
{
    MCLOG_WARN() << "[login] 连接聊天服务器失败:" << reason;
    fail(tr("连接聊天服务器失败，请稍后重试"));
}

void LoginController::onChatLoginSucceeded(const std::uint64_t uid, const QString& name,
                                           const QString& token)
{
    auto& session = mychat::SessionManager::instance();
    session.update(uid, name, token);

    m_user_label = session.label();
    emit userLabelChanged();

    MCLOG_INFO() << "[login] 聊天服登录成功:" << m_user_label;

    setBusy(false);
    setTip(tr("登录成功"));
    emit loginSucceeded();
}

void LoginController::onChatLoginFailed(const int error)
{
    MCLOG_WARN() << "[login] 聊天服登录失败, error =" << error;
    fail(mychat::protocol::errorToTip(error));
}

void LoginController::onLinkError(const QString& reason)
{
    MCLOG_WARN() << "[login] 链路异常:" << reason;
    fail(mychat::protocol::errorToTip(static_cast<int>(ErrorCode::NetworkError)));
}

void LoginController::fail(const QString& reason)
{
    setTip(reason, true);
    setBusy(false);
    emit loginFailed(reason);
}

void LoginController::setTip(const QString& text, const bool is_error)
{
    if (m_tip == text && m_tip_is_error == is_error)
        return;

    m_tip = text;
    m_tip_is_error = is_error;
    emit tipChanged();
    emit tipIsErrorChanged();
}

void LoginController::setBusy(const bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}
