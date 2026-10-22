#include "LoginController.h"

#include <QDateTime>
#include <QJsonObject>
#include <QVariantList>
#include <QVariantMap>

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
    connect(&socket, &mychat::ChatSocket::userInfoChangeResult,
            this, &LoginController::onUserInfoChangeResult);
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

QString LoginController::uid() const
{
    // 未登录时 uid 为 0，这里返回空串而不是 "0"：
    // QML 侧比的是字符串，空串才不会跟某个真实 uid 撞上。
    const std::uint64_t value = mychat::SessionManager::instance().uid();
    return value == 0 ? QString() : QString::number(value);
}

QString LoginController::name() const
{
    return mychat::SessionManager::instance().name();
}

void LoginController::login(const QString& account, const QString& password)
{
    if (m_busy)
        return;

    // 用户手输密码 = 这一轮不是"用存下来的 token"，失败时不该清凭据
    m_saved_login = false;

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

void LoginController::changeUserInfo(const QString& name, const QString& desc, const int sex)
{
    if (m_busy)
        return;

    auto& socket = mychat::ChatSocket::instance();
    if (!socket.isLoggedIn())
    {
        setTip(QStringLiteral("未连接到聊天服务器，请重新登录后再试"), true);
        emit userInfoChangeFinished(false, static_cast<int>(ErrorCode::NetworkError));
        return;
    }

    /*
     * 昵称沿用注册时的同一套规则（2~16 位、不含空白与 @）。
     * 服务端对 1031 完全不做校验 —— 空串会被原样写进库，之后好友列表里
     * 这个人就没有名字了，所以这一关必须客户端自己守。
     */
    const QString new_name = name.trimmed();
    const QString name_error = AccountValidator::checkNickname(new_name);
    if (!name_error.isEmpty())
    {
        setTip(name_error, true);
        emit validationFailed(QStringLiteral("name"));
        return;
    }

    // 签名长度没有服务端约束，但太长会把资料卡撑变形；与备注同量级即可
    const QString new_desc = desc.trimmed();
    if (new_desc.size() > mychat::protocol::MAX_FRIEND_REMARK_LEN * 2)
    {
        setTip(QStringLiteral("个性签名最多 %1 个字符")
                   .arg(mychat::protocol::MAX_FRIEND_REMARK_LEN * 2), true);
        emit validationFailed(QStringLiteral("desc"));
        return;
    }

    setBusy(true);
    setTip(QStringLiteral("正在保存资料…"));

    if (!socket.changeUserInfo(new_name, new_desc, sex))
    {
        // 与其它控制器同一条规矩：setBusy(false) 放最后
        setTip(QStringLiteral("资料保存请求发送失败"), true);
        setBusy(false);
    }
}

void LoginController::onUserInfoChangeResult(const bool ok, const int error, const QString& name,
                                              const QString& desc, const int sex)
{
    if (!ok)
    {
        setTip(mychat::protocol::errorToTip(error), true);
        emit userInfoChangeFinished(false, error);
        setBusy(false);
        return;
    }

    auto& session = mychat::SessionManager::instance();

    /*
     * 昵称同步进 SessionManager 并落盘：账号卡片上显示的就是它，下次自动登录
     * 也从这里读。token 原样带过去 —— update() 只改 uid / name / token 三项。
     */
    session.update(session.uid(), name, session.token());
    if (!session.save())
        qWarning() << "[login] 资料已改，但昵称没能写进本地凭据文件";

    m_self_desc = desc;
    m_self_sex = (sex == 0 || sex == 1) ? sex : 2;
    emit selfProfileChanged();

    m_user_label = session.label();
    emit userLabelChanged();
    emit accountsChanged();

    setTip(QStringLiteral("资料已保存"));
    setBusy(false);

    MCLOG_INFO() << "[login] 资料已更新, 新昵称 =" << name;

    emit userInfoChangeFinished(true, kSuccess);
}

void LoginController::logout()
{
    // 先断链再清会话：顺序反了的话 socket 那边可能还在用旧 uid 发帧
    mychat::ChatSocket::instance().disconnectFromServer();
    mychat::SessionManager::instance().clear();

    m_uid = 0;
    m_token.clear();
    m_host.clear();
    m_port = 0;
    m_saved_login = false;

    /*
     * 主动退出 = 断开连接 + 回登录页，**凭据留着**。
     *
     * 这样退出后点一下卡片就能直接回去（密码不用再输一遍）—— 退出登录的
     * 意思是"我现在不想连着"，不是"把这台机器记住的号忘掉"。真要忘，
     * 卡片上的「移除」才是那个动作（forgetAccount 连凭据一起删）。
     *
     * 代价是单账号时关掉程序再打开还会自动登录。这是有意的：QQ / 微信都是
     * 这个语义，而"彻底不再自动登"由「移除」兜底。
     *
     * 凭据本身被服务端作废（1012 / 1011）时由 onChatLoginFailed 负责清掉，
     * 那条路径不动，否则点卡片会一直撞同一堵墙。
     */
    // 但下次启动别再自己登进去：退出就是退出，重新进要用户点一下卡片
    mychat::SessionManager::instance().setSkipAutoLogin(true);
    emit accountsChanged();

    m_user_label.clear();
    emit userLabelChanged();

    // 主动退出不是"链路异常"，提示条不该留着
    setLinkError(QString());

    MCLOG_INFO() << "[login] 已退出登录";
}

QVariantList LoginController::accounts() const
{
    auto& session = mychat::SessionManager::instance();
    const std::uint64_t last_uid = session.lastUid();

    QVariantList list;
    for (const mychat::SessionManager::Account& account: session.accounts())
    {
        QVariantMap item;
        item.insert(QStringLiteral("uid"), QString::number(account.uid));
        item.insert(QStringLiteral("name"), account.name);
        item.insert(QStringLiteral("hasToken"), account.hasToken());
        item.insert(QStringLiteral("isLast"), account.uid == last_uid);
        item.insert(QStringLiteral("lastText"),
                    account.last_login_ms > 0
                        ? QDateTime::fromMSecsSinceEpoch(account.last_login_ms)
                              .toString(QStringLiteral("MM-dd HH:mm"))
                        : QString());
        list.append(item);
    }
    return list;
}

void LoginController::selectAccount(const QString& uid)
{
    if (m_selected_uid == uid)
        return;

    m_selected_uid = uid;
    emit selectedUidChanged();
}

bool LoginController::loginWithAccount(const QString& uid)
{
    if (m_busy)
        return false;

    bool uid_ok = false;
    const std::uint64_t target = uid.toULongLong(&uid_ok);
    if (!uid_ok || target == 0)
        return false;

    auto& session = mychat::SessionManager::instance();
    session.clear();
    if (!session.loadAccount(target))
    {
        /*
         * 卡片还在但这个号没有可用凭据（退出过登录 / token 被顶掉）。
         * 只切选中，让 QML 把账号填进输入框、引导输密码 —— 这里不算失败，
         * 所以不打 tip（用户点卡片时看到一句红字只会莫名其妙）。
         */
        selectAccount(uid);
        return false;
    }

    m_uid = session.uid();
    m_token = session.token();
    m_host = session.host();
    m_port = session.port();
    m_saved_login = true;
    selectAccount(QString::number(m_uid));

    setBusy(true);
    setTip(tr("正在进入…"));

    MCLOG_INFO() << "[login] 用存下的凭据进入: uid =" << m_uid
        << "host =" << m_host << "port =" << m_port;

    mychat::ChatSocket::instance().connectToServer(m_host, m_port);
    return true;
}

void LoginController::forgetAccount(const QString& uid)
{
    bool uid_ok = false;
    const std::uint64_t target = uid.toULongLong(&uid_ok);
    if (!uid_ok || target == 0)
        return;

    mychat::SessionManager::instance().forgetAccount(target);
    if (m_selected_uid == uid)
        selectAccount(QString());
    emit accountsChanged();

    MCLOG_INFO() << "[login] 用户移除了账号卡片:" << uid;
}

bool LoginController::tryAutoLogin()
{
    if (m_busy)
        return false;

    auto& session = mychat::SessionManager::instance();
    // 先清内存再读盘：上一次会话可能停在半路（比如登录失败后重试），
    // 残留的 uid/token 不能参与这一轮。
    session.clear();

    const QList<mychat::SessionManager::Account> list = session.accounts();
    if (list.isEmpty())
        return false;

    const bool forced = qEnvironmentVariableIsSet("MYCHAT_AUTOLOGIN_SAVED");

    /*
     * 上次是用户主动退出的 → 停在登录页，别自作主张登回去。
     *
     * 凭据没删（点卡片照样免密进），只是"开机自动进"这件事要用户点头。
     */
    if (session.skipAutoLogin() && !forced)
    {
        const std::uint64_t last = session.lastUid();
        const std::uint64_t pick = last != 0 ? last : list.first().uid;
        session.loadAccount(pick);
        selectAccount(QString::number(pick));
        MCLOG_INFO() << "[login] 上次是主动退出，停在登录页，默认选中" << pick;
        return false;
    }

    /*
     * 多账号时**不自动进**：登录页那排卡片就是给用户选的，直接跳进聊天页
     * 等于把入口藏起来了。
     *
     * MYCHAT_AUTOLOGIN_SAVED=1 无视这条规则 —— 离屏验证要的是"自动登录本身"
     * 走通，而不是"多账号就停下来"。
     */
    if (list.size() > 1 && !forced)
    {
        const std::uint64_t last = session.lastUid();
        const std::uint64_t pick = last != 0 ? last : list.first().uid;
        session.loadAccount(pick);
        selectAccount(QString::number(pick));
        MCLOG_INFO() << "[login]" << list.size() << "个账号，停在登录页选卡，默认选中" << pick;
        return false;
    }

    // 单账号：和以前一样直接进（前提是它还有免密凭据）
    const std::uint64_t last = session.lastUid();
    const std::uint64_t target = last != 0 ? last : list.first().uid;
    if (!session.loadAccount(target))
    {
        // 卡片还在但 token 被清过（比如上次点了退出登录）→ 停在登录页等输密码
        selectAccount(QString::number(target));
        return false;
    }

    m_uid = session.uid();
    m_token = session.token();
    m_host = session.host();
    m_port = session.port();
    m_saved_login = true;
    selectAccount(QString::number(m_uid));

    setBusy(true);
    setTip(tr("正在恢复上次登录…"));

    MCLOG_INFO() << "[login] 自动登录: uid =" << m_uid
        << "host =" << m_host << "port =" << m_port;

    // 直接连存下来的地址：拿不到新地址（那要走 HTTP，需要密码），只能信上次的这个。
    // 连不上就走 onSocketConnectFailed —— 那种情况**不删**凭据（可能只是服务端没起）。
    mychat::ChatSocket::instance().connectToServer(m_host, m_port);
    return true;
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
    m_host = host;
    m_port = port;
    mychat::SessionManager::instance().setEndpoint(host, port);

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

    /*
     * 走到这里 token 已经过服务端校验，是"确定能用"的凭据 —— 现在才落盘。
     *
     * 自动登录成功时这一句是幂等的（盘上本来就是这份数据），重写一遍没有副作用；
     * 手动登录成功时则是把新 token / 新地址写进去（每次 /user_login 都会换 token，
     * 旧的那份从这一刻起就作废了）。
     */
    m_saved_login = false;
    session.save();
    // 登录成功 = "退出登录"那条禁令作废，下次启动恢复正常自动登录
    session.setSkipAutoLogin(false);
    // 卡片上的"上次登录"时间 / 排序 / 是否免密都变了，通知登录页刷新
    emit accountsChanged();

    // 换账号 / 重新登录：上一轮编辑过的签名和性别不能带过来（服务端也不回发）
    if (!m_self_desc.isEmpty() || m_self_sex != 2)
    {
        m_self_desc.clear();
        m_self_sex = 2;
        emit selfProfileChanged();
    }

    m_user_label = session.label();
    emit userLabelChanged();

    MCLOG_INFO() << "[login] 聊天服登录成功:" << m_user_label;

    // 重新连上以后"最近一次链路异常"就过期了，别让提示条继续挂着
    setLinkError(QString());
    setBusy(false);
    setTip(tr("登录成功"));
    emit loginSucceeded();
}

void LoginController::onChatLoginFailed(const int error)
{
    MCLOG_WARN() << "[login] 聊天服登录失败, error =" << error;

    /*
     * 用存下来的 token 进不去，只有两种情况是"凭据本身作废"：
     *   · 1012 token 与 StatusServer 记录不一致（在别处登录把它顶掉了）；
     *   · 1011 StatusServer 里根本没有这个 uid 的 token（Redis 被清 / 过期）。
     * 这两种留着凭据也连不上，删掉（卡片保留）免得下次再撞一次。
     *
     * 其它错误（1002 调 StatusServer 失败、1013 Redis 不可用）是服务端一时的
     * 问题，凭据还是好的 —— 清了反而要用户重输密码。
     */
    if (m_saved_login && (error == 1011 || error == 1012))
    {
        const std::uint64_t uid = mychat::SessionManager::instance().uid();
        m_saved_login = false;
        mychat::SessionManager::instance().dropToken(uid);
        emit accountsChanged();

        setTip(tr("登录状态已失效，请重新登录"), true);
        setBusy(false);
        emit loginFailed(tr("登录状态已失效，请重新登录"));
        return;
    }

    fail(mychat::protocol::errorToTip(error));
}

void LoginController::onLinkError(const QString& reason)
{
    MCLOG_WARN() << "[login] 链路异常:" << reason;

    // 把原因原样带上，别替换成一句"网络连接异常"：断连有可能是服务端回了畸形帧、
    // 也可能是对端主动关连接，抹平之后用户和日志都只剩"网络问题"，排查方向直接跑偏。
    setLinkError(reason.isEmpty()
                     ? mychat::protocol::errorToTip(static_cast<int>(ErrorCode::NetworkError))
                     : reason);

    fail(m_link_error);
}

void LoginController::setLinkError(const QString& text)
{
    if (m_link_error == text)
        return;

    m_link_error = text;
    emit linkErrorChanged();
}

void LoginController::fail(const QString& reason)
{
    /*
     * 用存下来的 token 却连不上服务器（端口变了 / 服务端没起）：
     * **不删凭据** —— 这是服务端的事，凭据本身可能还是好的，
     * 删了等于让用户因为别人重启服务而重输一次密码。
     * 只把标志清掉，提示照常给。
     */
    m_saved_login = false;

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
