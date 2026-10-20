#pragma once

#include <QList>
#include <QString>

#include <cstdint>

namespace mychat
{
    /**
 * 当前登录会话 + 本机记住的账号列表。
 *
 * 由 ChatSocket 在登录握手成功后写入，供 UI 展示与后续业务请求取用。
 * 纯数据 + 极简访问器，不继承 QObject —— 需要在 QML 里读取时，
 * 统一由控制器（如 LoginController）转成属性暴露。
 *
 * ## 为什么要落盘（以及为什么是"多账号"）
 *
 * 服务端的 token 走的 `redisSet(USER_TOKEN_PREFIX + uid, token)` —— **不带 TTL**，
 * 一直有效到下一次 `/user_login` 覆盖；而 `1001` 握手只校验 `{uid, token}`，
 * 不经过 HTTP、不需要密码。于是「下次启动直接连上去」这件事是白送的。
 *
 * token 的 Redis 键**按 uid 分开**，所以同一台机器存多个账号的 token 互不影响 ——
 * 登录页那排可左右滑动的账号卡片就是靠这个：每个账号一份凭据，选中即可免密进入。
 *
 * 存 token 而不是密码：密码能推出 token，token 推不出密码；而且在别处登录会把
 * 该 uid 的 token 覆盖掉，本机的对应卡片自然降级成"要输密码"，语义正好。
 *
 * ## 存在哪
 *
 * 独立 INI：`QStandardPaths::AppConfigLocation/session.ini`，Windows 上是
 * `%APPDATA%/LocalLow/MyChat/MyChat/session.ini`。选文件而不是注册表：
 * 用户想清可以直接删、能跟着备份工具走、写失败时能看到真实的错误码。
 * `MYCHAT_SESSION_FILE=<path>` 可以改成别处（自检程序靠它隔离存储）。
 *
 * 结构：
 * ```
 * [Session]
 * lastUid=1001                  ; 上次成功登录的账号，登录页默认选中它
 * [Accounts]
 * 1001\uid=1001
 * 1001\name=norym
 * 1001\token=f917...
 * 1001\host=127.0.0.1
 * 1001\port=8091
 * 1001\lastLoginMs=1760...
 * ```
 */
    class SessionManager
    {
    public:
        /// 本机记住的一个账号
        struct Account
        {
            std::uint64_t uid = 0;
            QString name;
            QString token;
            QString host;
            std::uint16_t port = 0;
            /// 上次成功登录的时间戳（毫秒），用来排序与显示"上次登录"标记
            qint64 last_login_ms = 0;

            /// 有可用的免密凭据（token / 地址 / 端口齐全）
            [[nodiscard]] bool hasToken() const
            {
                return uid != 0 && !token.isEmpty() && !host.isEmpty() && port != 0;
            }
        };

        static SessionManager& instance();

        SessionManager(const SessionManager&) = delete;
        SessionManager& operator=(const SessionManager&) = delete;

        // ── 内存里的「当前会话」────────────────────────────────────────

        [[nodiscard]] std::uint64_t uid() const { return m_uid; }
        [[nodiscard]] QString name() const { return m_name; }
        [[nodiscard]] QString token() const { return m_token; }

        /**
         * 聊天服地址。由 LoginController 在 HTTP 登录回包里拿到后写入 ——
         * ChatSocket 登录成功时并不知道这个值（它只知道连上了哪儿），
         * 所以没有并进 update()，而是单独一个入口。
         */
        [[nodiscard]] QString host() const { return m_host; }
        [[nodiscard]] std::uint16_t port() const { return m_port; }

        void update(std::uint64_t uid, const QString& name, const QString& token);
        void setEndpoint(const QString& host, std::uint16_t port);
        void clear();

        // ── 本机账号列表 ───────────────────────────────────────────────

        /// 所有记住的账号，按上次登录时间倒序（最近用的排最前）
        [[nodiscard]] QList<Account> accounts() const;

        /// 上次成功登录的 uid；没有记录时 0
        [[nodiscard]] std::uint64_t lastUid() const;

        /**
         * 上次是不是用户**主动点了退出登录**。
         *
         * 退出登录不删凭据（卡片还能免密进），但"下次启动别再自动进去"这件事
         * 得单独记一笔 —— 否则用户点了退出、关掉程序、再打开，又自己登进去了，
         * 那个按钮等于没用。
         */
        [[nodiscard]] bool skipAutoLogin() const;
        void setSkipAutoLogin(bool skip);

        /**
         * 把某个账号的凭据读进内存（覆盖当前会话）。
         *
         * @return 该账号存在且 token / 地址齐全时 true —— 也就是"可以免密进入"。
         *         卡片还在但 token 被清过时返回 false，调用方应引导输密码。
         */
        bool loadAccount(std::uint64_t uid);

        /**
         * 把**内存里的当前会话**写进账号列表（并更新 lastUid）。
         *
         * 在聊天服登录成功后调用：那一刻 token 已经过服务端校验，落盘的是
         * 确定能用的凭据；HTTP 登录刚返回就写的话，可能写进一个连不上聊天服的地址。
         *
         * @return 真的写了才 true（字段不齐时返回 false，不写脏数据）
         */
        bool save() const;

        /// 彻底移除一个账号卡片（连同凭据）
        void forgetAccount(std::uint64_t uid);

        /**
         * 只删某个账号的凭据，保留卡片（uid / 昵称 / 上次登录时间）。
         *
         * 用在两个地方：用户主动退出登录（下次要输密码，但账号还在列表里），
         * 以及服务端回了 1012（token 被别处登录顶掉 —— 留着也连不上）。
         */
        void dropToken(std::uint64_t uid);

        /// 清空整张账号列表（内存不动）
        void forgetSaved();

        /// 记录所在文件，仅供日志与自检程序定位
        [[nodiscard]] static QString storagePath();

        /// 展示用文案："UID 1001 · norym"（昵称为空时退化为 "UID 1001"）
        [[nodiscard]] QString label() const;

    private:
        SessionManager() = default;

        /**
         * 把旧的单账号结构（`[Session] uid/token/host/port`）搬进 `[Accounts]`。
         *
         * 0.1 版本只存一个账号，用户升级后不该被"重新登录一次"惩罚。
         * 幂等：搬完删掉旧键，重复调用无副作用。
         */
        static void migrateLegacy();

        std::uint64_t m_uid = 0;
        QString m_name;
        QString m_token;
        QString m_host;
        std::uint16_t m_port = 0;
    };
} // namespace mychat
