#include "LoginController.h"

#include <QJsonObject>
#include <QRegularExpression>

#include "ChatSocket.h"
#include "HttpClient.h"
#include "SessionManager.h"
#include "logger.h"
#include "protocol.h"

namespace
{
    using mychat::protocol::ErrorCode;

    /// 账号：4 位以上数字且首位不为 0
    const QRegularExpression kAccountRe(QStringLiteral(R"(^[1-9][0-9]{3,}$)"));

    /// 密码：6~15 位，字母 / 数字 / !@#$%^&*
/// 注意这里**不包含**下划线与点号 —— 必须与服务端实际接受的范围一致，
/// 否则会出现「本地放行、服务端拒绝」或反之的错位。
    const QRegularExpression kPasswordRe(QStringLiteral(R"(^[a-zA-Z0-9!@#$%^&*]{6,15}$)"));

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
    if (text.isEmpty())
        return tr("请输入账号");
    if (!kAccountRe.match(text).hasMatch())
        return tr("账号格式错误：需为 4 位以上数字，且首位不能是 0");
    return {};
}

QString LoginController::CheckPassword(const QString& text)
{
    if (text.isEmpty())
        return tr("请输入密码");
    if (!kPasswordRe.match(text).hasMatch())
        return tr("密码格式错误：6~15 位，仅限字母、数字与 !@#$%^&* 符号");
    return {};
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
        QStringLiteral("/user_login"), body,
        [this](const bool ok, const QJsonObject& resp) { onHttpLoginDone(ok, resp); });
}

void LoginController::onHttpLoginDone(const bool ok, const QJsonObject& resp)
{
    if (!ok)
    {
        setBusy(false);
        setTip(mychat::protocol::errorToTip(
                   resp.value(QStringLiteral("error")).toInt()),
               true);
        return;
    }

    // 文档：业务失败同样是 HTTP 200，要再看 body 里的 error
    const int error = resp.value(QStringLiteral("error"))
                          .toInt(static_cast<int>(ErrorCode::ServerJsonError));
    if (error != kSuccess)
    {
        setBusy(false);
        setTip(mychat::protocol::errorToTip(error), true);
        return;
    }

    // uid 由服务端以 uint64 字符串形式下发（用 toInt() 会截断大 uid）
    const std::optional<std::uint64_t> uid =
        mychat::protocol::parseUid(resp.value(QStringLiteral("uid")));
    if (!uid.has_value())
    {
        MCLOG_WARN() << "[login] 登录响应里的 uid 非法:" << resp;
        setBusy(false);
        setTip(tr("服务器返回数据不完整"), true);
        return;
    }

    const QString token = resp.value(QStringLiteral("token")).toString();
    const QString host = resp.value(QStringLiteral("host")).toString();
    const QString port_text = resp.value(QStringLiteral("port")).toString();

    if (token.isEmpty() || host.isEmpty() || port_text.isEmpty())
    {
        MCLOG_WARN() << "[login] 登录响应字段不完整:" << resp;
        setBusy(false);
        setTip(tr("服务器返回数据不完整"), true);
        return;
    }

    bool be_valid_port = false;
    const std::uint16_t port = port_text.toUShort(&be_valid_port);
    if (!be_valid_port || port == 0)
    {
        MCLOG_WARN() << "[login] 聊天服端口非法:" << port_text;
        setBusy(false);
        setTip(tr("服务器返回的聊天服端口无效"), true);
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
    {
        setBusy(false);
        setTip(tr("登录消息发送失败"), true);
    }
}

void LoginController::onSocketConnectFailed(const QString& reason)
{
    MCLOG_WARN() << "[login] 连接聊天服务器失败:" << reason;
    setBusy(false);
    setTip(tr("连接聊天服务器失败，请稍后重试"), true);
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
    setBusy(false);
    setTip(mychat::protocol::errorToTip(error), true);
}

void LoginController::onLinkError(const QString& reason)
{
    MCLOG_WARN() << "[login] 链路异常:" << reason;
    setBusy(false);
    setTip(mychat::protocol::errorToTip(static_cast<int>(ErrorCode::NetworkError)), true);
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
