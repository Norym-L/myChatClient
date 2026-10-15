#include "SessionManager.h"

namespace mychat
{
    SessionManager& SessionManager::instance()
    {
        static SessionManager manager;
        return manager;
    }

    void SessionManager::update(const std::uint64_t uid, const QString& name, const QString& token)
    {
        m_uid = uid;
        m_name = name;
        m_token = token;
    }

    void SessionManager::clear()
    {
        m_uid = 0;
        m_name.clear();
        m_token.clear();
    }

    QString SessionManager::label() const
    {
        if (m_name.isEmpty())
            return QStringLiteral("UID %1").arg(m_uid);
        return QStringLiteral("UID %1 · %2").arg(m_uid).arg(m_name);
    }
} // namespace mychat
