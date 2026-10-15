#pragma once

#include <QString>

#include <cstdint>

namespace mychat
{
    /**
 * 当前登录会话（uid / 昵称 / token）。
 *
 * 由 ChatSocket 在登录握手成功后写入，供 UI 展示与后续业务请求取用。
 * 纯数据 + 极简访问器，不继承 QObject —— 需要在 QML 里读取时，
 * 统一由控制器（如 LoginController）转成属性暴露。
 */
    class SessionManager
    {
    public:
        static SessionManager& instance();

        SessionManager(const SessionManager&) = delete;
        SessionManager& operator=(const SessionManager&) = delete;

        [[nodiscard]] std::uint64_t uid() const { return m_uid; }
        [[nodiscard]] QString name() const { return m_name; }
        [[nodiscard]] QString token() const { return m_token; }

        void update(std::uint64_t uid, const QString& name, const QString& token);
        void clear();

        /// 展示用文案："UID 1001 · norym"（昵称为空时退化为 "UID 1001"）
        [[nodiscard]] QString label() const;

    private:
        SessionManager() = default;

        std::uint64_t m_uid = 0;
        QString m_name;
        QString m_token;
    };
} // namespace mychat
