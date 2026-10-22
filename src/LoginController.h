#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

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
    /// 当前登录 uid（未登录时为空串）。QML 用它判断"这个人是不是自己"
    Q_PROPERTY(QString uid READ uid NOTIFY userLabelChanged)
    /// 当前登录昵称（未登录时为空串）
    Q_PROPERTY(QString name READ name NOTIFY userLabelChanged)

    /**
     * 本人的签名 / 性别（**本次会话的缓存**，不是服务端查询结果）。
     *
     * 服务端没有"查自己资料"的接口：登录回包只带 uid + 昵称。所以这两个值
     * 只能来自本次登录期间的编辑（1031 成功回填）—— 重启后就回到空 / 2，
     * 直到用户再改一次。留成属性是为了「我的资料」弹窗能显示刚改过的值，
     * 而不是每次打开都回到"服务端暂不下发"的空态。
     */
    Q_PROPERTY(QString selfDesc READ selfDesc NOTIFY selfProfileChanged)
    Q_PROPERTY(int selfSex READ selfSex NOTIFY selfProfileChanged)

    /**
     * 最近一次链路异常的原因（空串 = 链路正常）。
     *
     * 聊天页据此显示一条提示条：连接断了以后 `ChatSocket` 只会静默丢弃后续请求，
     * 界面上原来**什么提示都没有**，用户只会以为"卡住了"。
     * 登录成功 / 主动退出时清空。
     */
    Q_PROPERTY(QString linkError READ linkError NOTIFY linkErrorChanged)

    /**
     * 本机记住的账号卡片列表（登录页那排可左右滑动的卡）。
     *
     * 每项：`{ uid, name, hasToken, lastText, isLast }`
     *   · `hasToken`  true = 有免密凭据，点「进入」就能进；
     *   · `lastText`  上次登录时间（"10-20 23:31"），没登录过是空串；
     *   · `isLast`    是不是上次用的那个（卡片上打「上次」标记）。
     *
     * 只在登录页活着的时候用，但由控制器提供 —— QML 不该自己去读 session.ini。
     */
    Q_PROPERTY(QVariantList accounts READ accounts NOTIFY accountsChanged)

    /// 当前在卡片条里选中的 uid（空串 = 没有选中，走"新账号"表单）
    Q_PROPERTY(QString selectedUid READ selectedUid NOTIFY selectedUidChanged)

public:
    explicit LoginController(QObject* parent = nullptr);

    [[nodiscard]] QString tip() const { return m_tip; }
    [[nodiscard]] bool tipIsError() const { return m_tip_is_error; }
    [[nodiscard]] bool busy() const { return m_busy; }
    [[nodiscard]] QString userLabel() const { return m_user_label; }
    [[nodiscard]] QString linkError() const { return m_link_error; }
    [[nodiscard]] QVariantList accounts() const;
    [[nodiscard]] QString selectedUid() const { return m_selected_uid; }

    /**
     * 当前 uid / 昵称，直接取自 SessionManager。
     *
     * 不缓存成成员变量：uid 的唯一事实来源在 SessionManager，本类再存一份就等于
     * 有了第二处真相 —— 迟早会出现"登录了但控制器里还是旧 uid"这类偏差。
     * 两者都挂在 userLabelChanged 上通知：登录 / 退出都会发它，正好覆盖生命周期。
     */
    [[nodiscard]] QString uid() const;
    [[nodiscard]] QString name() const;
    [[nodiscard]] QString selfDesc() const { return m_self_desc; }
    [[nodiscard]] int selfSex() const { return m_self_sex; }

    /// 校验账号：返回空串表示通过，否则是给用户看的错误提示。
    /// 无状态所以是 static；注意 Q_INVOKABLE 对 static 函数无效（QML 访问不到），
    /// 校验统一在 login() 内部完成。
    [[nodiscard]] static QString CheckAccount(const QString& text);
    /// 校验密码：同上
    [[nodiscard]] static QString CheckPassword(const QString& text);

    /// 发起登录（本地校验 → HTTP → 连接 ChatServer → TCP 登录握手）
    Q_INVOKABLE void login(const QString& account, const QString& password);

    /**
     * 自动登录：用上次成功登录后存下的 `{uid, token, host, port}` 直接连聊天服。
     *
     * 跳过 HTTP（不需要密码）—— `1001` 握手只校验 uid + token，而服务端的 token
     * 是不带 TTL 的 Redis 键，一直有效到下一次 `/user_login` 覆盖。
     *
     * @return 磁盘上有完整记录、已经发起连接时 true（结果仍然走 loginSucceeded /
     *         loginFailed）；没有记录、或当前正忙时 false（调用方什么都不用做）。
     *
     * 失败时会把磁盘记录一起清掉：token 被顶、聊天服换了端口、服务端没起，
     * 这几种情况重试都是白搭，留着只会让下次启动再卡一遍。
     */
    Q_INVOKABLE bool tryAutoLogin();

    /**
     * 在账号卡片条里选中某个账号（只切高亮，不发请求）。
     *
     * QML 会在选中的同时把 uid 填进账号输入框 —— 用户选完还能改，改的就是
     * "用另一个号登录"，不会被卡片绑死。
     */
    Q_INVOKABLE void selectAccount(const QString& uid);

    /**
     * 用某个账号存下的 token 免密进入（卡片上的「进入」）。
     *
     * @return 已经发起连接时 true；该账号没有可用凭据（或正忙）时 false，
     *         调用方应退回密码登录。
     *
     * 失败时只清**这个账号的 token**，卡片保留 —— 用户还能用密码登同一个号。
     */
    Q_INVOKABLE bool loginWithAccount(const QString& uid);

    /// 从卡片列表里彻底移除某个账号（连凭据一起）
    Q_INVOKABLE void forgetAccount(const QString& uid);

    /// 退出登录：断开与 ChatServer 的连接并清空会话（不清空 tip，方便页面提示）
    Q_INVOKABLE void logout();

    /**
     * 编辑本人资料（1031 / 1032）。
     *
     * 服务端允许只传想改的字段，但编辑表单一次提交三项，所以这里全发 ——
     * 只改签名时原昵称也要带上，否则空串会被当成"把昵称改成空"写进库。
     * 头像（`icon`）不发：服务端没有上传通道，发了只会覆盖已有地址。
     *
     * 成功后昵称会同步进 SessionManager 并落盘（账号卡片显示的就是它），
     * 签名 / 性别缓存到本对象，供「我的资料」弹窗回显（服务端没有查自己的接口）。
     */
    Q_INVOKABLE void changeUserInfo(const QString& name, const QString& desc, int sex);

    signals :

    void tipChanged();
    void tipIsErrorChanged();
    void busyChanged();
    void userLabelChanged();
    void selfProfileChanged();
    void linkErrorChanged();
    void accountsChanged();
    void selectedUidChanged();
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

    /**
     * 编辑本人资料结束（1031 / 1032）。
     *
     * ok=false 时 error：1011（会话未登录 / MySQL 里没有这个 uid）、
     * 1013（读不到用户缓存或数据库失败）、5（本地等回包超时）。
     * 本地校验失败（昵称为空）不走这里 —— 那种情况只发 validationFailed。
     */
    void userInfoChangeFinished(bool ok, int error);

private:
    /// 设置提示文案；is_error 决定 QML 里的显示颜色
    void setTip(const QString& text, bool is_error = false);
    void setBusy(bool busy);
    /// 记录 / 清空链路异常原因（空串 = 正常）
    void setLinkError(const QString& text);

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
    /// 编辑资料回包（1032）
    void onUserInfoChangeResult(bool ok, int error, const QString& name,
                                const QString& desc, int sex);

    QString m_tip;
    bool m_tip_is_error = false;
    bool m_busy = false;
    QString m_user_label;
    /// 本次会话里改过的签名 / 性别（服务端没有"查自己"的接口，只能本地留）
    QString m_self_desc;
    int m_self_sex = 2;
    /// 最近一次链路异常原因；空串表示链路正常
    QString m_link_error;

    // HTTP 登录响应拿到、随后用于 TCP 登录帧的凭据
    std::uint64_t m_uid = 0;
    QString m_token;

    /**
     * 这一轮登录用的是「存下来的 token」（true）还是用户手输密码（false）。
     *
     * 失败出口靠它决定两件事：要不要把这个账号的免密凭据删掉（token 被顶掉 →
     * 留着也连不上），以及提示文案要不要换成"登录状态已失效"。
     */
    bool m_saved_login = false;

    /// 卡片条里选中的账号（空串 = 走"新账号"表单）
    QString m_selected_uid;

    // 聊天服地址：HTTP 登录回包里拿到，登录成功后与 token 一起落盘
    QString m_host;
    std::uint16_t m_port = 0;
};
