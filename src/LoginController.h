#pragma once

#include <QObject>
#include <QString>

#include <cstdint>

class QJsonObject;

/**
 * LoginController —— 登录链路控制器（QML 与 core 之间的唯一入口）
 *
 * 把「本地校验 → HTTP 登录 → 连接 ChatServer → TCP 登录握手 → 切换页面」
 * 整条链路封装在 C++ 里，QML 只负责收集输入、显示提示、响应状态：
 *
 *   LoginController.login(account, password)
 *     ├─ validationFailed(field)  本地校验失败，field 是 "account" / "password"
 *     ├─ tipChanged               过程提示或错误信息（读 LoginController.tip）
 *     └─ loginSucceeded           全链路成功，可以切到聊天页
 *
 * QML 不直接接触 HttpClient / ChatSocket：它们的接口是
 * QJsonObject / std::function 回调 / message_id 这套 C++ 形态，
 * 直接暴露既难用，也等于把协议约束（uid 必须按字符串传等）交给 QML 去守。
 */
class LoginController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString tip READ tip NOTIFY tipChanged)
    /// tip 是否为错误提示（QML 据此决定文字用危险色还是普通色）
    Q_PROPERTY(bool tipIsError READ tipIsError NOTIFY tipIsErrorChanged)
    /// 登录中的忙碌态（QML 据此禁用输入与按钮）
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    /// 登录成功后的用户标识，形如 "UID 1001 · norym"，供聊天页显示
    Q_PROPERTY(QString userLabel READ userLabel NOTIFY userLabelChanged)

public:
    explicit LoginController(QObject* parent = nullptr);

    [[nodiscard]] QString tip() const { return m_tip; }
    [[nodiscard]] bool tipIsError() const { return m_tip_is_error; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString userLabel() const { return m_user_label; }

    /// 校验账号：返回空串表示通过，否则是给用户看的错误提示。
    /// 无状态所以是 static；注意 Q_INVOKABLE 对 static 函数无效（QML 访问不到），
    /// 校验统一在 login() 内部完成。
    [[nodiscard]] static QString CheckAccount(const QString& text);
    /// 校验密码：同上
    [[nodiscard]] static QString CheckPassword(const QString& text);

    /// 发起登录（本地校验 → HTTP → 连接 ChatServer → TCP 登录握手）
    Q_INVOKABLE void login(const QString& account, const QString& password);

    signals :

    void tipChanged();
    void tipIsErrorChanged();
    void busyChanged();
    void userLabelChanged();
    /// 本地校验失败，field 为 "account" 或 "password"
    void validationFailed(const QString& field);
    /// 全链路登录成功（已与 ChatServer 完成握手）
    void loginSucceeded();
    /**
     * 登录失败（不含本地校验失败 —— 那种情况走 validationFailed）。
     *
     * 有它才能让调用方区分「还在进行中」和「已经失败」：只靠 tip 的话，
     * 自检程序只能干等到超时，一次失败要 20 秒才出结果。
     */
    void loginFailed(const QString& reason);

private:
    /// 设置提示文案；is_error 决定 QML 里的显示颜色
    void setTip(const QString& text, bool is_error = false);
    void setBusy(bool busy);

    /// 统一的失败出口：设错误提示 + 结束忙碌态 + 发 loginFailed。
    /// 分开写三句很容易漏掉其中一句（漏了 loginFailed 就是调用方干等到超时）。
    void fail(const QString& reason);

    // 链路各阶段的回调
    void onHttpLoginDone(bool ok, const QJsonObject& resp);
    void onSocketConnected();
    void onSocketConnectFailed(const QString& reason);
    void onChatLoginSucceeded(std::uint64_t uid, const QString& name, const QString& token);
    void onChatLoginFailed(int error);
    void onLinkError(const QString& reason);

    QString m_tip;
    bool m_tip_is_error = false;
    bool m_busy = false;
    QString m_user_label;

    // HTTP 登录响应拿到、随后用于 TCP 登录帧的凭据
    std::uint64_t m_uid = 0;
    QString m_token;
};
