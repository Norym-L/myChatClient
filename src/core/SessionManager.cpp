#include "SessionManager.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>
#include <memory>

#include "logger.h"

namespace mychat
{
    namespace
    {
        // 键名集中在这里：读写两处必须一模一样，散落在函数体里迟早写错一个字母，
        // 而 QSettings 写错键不会报错，只会"读回时是空的"。
        constexpr auto kSessionGroup = "Session";
        constexpr auto kAccountsGroup = "Accounts";
        constexpr auto kLastUid = "lastUid";
        constexpr auto kSkipAuto = "skipAuto";
        constexpr auto kUid = "uid";
        constexpr auto kName = "name";
        constexpr auto kToken = "token";
        constexpr auto kHost = "host";
        constexpr auto kPort = "port";
        constexpr auto kLastLogin = "lastLoginMs";

        /**
         * 打开存会话的那份 QSettings。
         *
         * 默认是一个独立的 INI 文件（`%APPDATA%/LocalLow/MyChat/MyChat/session.ini`）：
         *   · 用户/运维可以直接打开看、直接删，不用开注册表编辑器；
         *   · 也可以被备份工具带走，换机后仍然免登录；
         *   · 受限环境里写不进去时 `status()` 会如实报错，不像注册表那样
         *     只能看到一句 AccessError。
         *
         * `MYCHAT_SESSION_FILE=<path>` 可以指定别处 —— 自检程序和离屏验证靠它
         * 隔离存储，不碰用户真实配置。
         */
        QString defaultSettingsFile()
        {
            QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
            if (dir.isEmpty())
                dir = QCoreApplication::applicationDirPath();
            return dir + QStringLiteral("/session.ini");
        }

        std::unique_ptr<QSettings> openSettings()
        {
            QString path = qEnvironmentVariable("MYCHAT_SESSION_FILE");
            if (path.isEmpty())
                path = defaultSettingsFile();
            return std::make_unique<QSettings>(path, QSettings::IniFormat);
        }
    } // namespace

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

    void SessionManager::setEndpoint(const QString& host, const std::uint16_t port)
    {
        m_host = host;
        m_port = port;
    }

    void SessionManager::clear()
    {
        m_uid = 0;
        m_name.clear();
        m_token.clear();
        m_host.clear();
        m_port = 0;
    }

    void SessionManager::migrateLegacy()
    {
        const std::unique_ptr<QSettings> settings = openSettings();

        settings->beginGroup(QLatin1String(kSessionGroup));
        const QString legacy_uid = settings->value(QLatin1String(kUid)).toString();
        if (legacy_uid.isEmpty())
        {
            settings->endGroup();
            return;
        }

        const QString name = settings->value(QLatin1String(kName)).toString();
        const QString token = settings->value(QLatin1String(kToken)).toString();
        const QString host = settings->value(QLatin1String(kHost)).toString();
        const int port = settings->value(QLatin1String(kPort), 0).toInt();
        settings->endGroup();

        bool uid_ok = false;
        const std::uint64_t uid = legacy_uid.toULongLong(&uid_ok);
        if (!uid_ok || uid == 0)
            return;

        // 先写进新的多账号结构，再删旧键 —— 顺序反了，中途崩溃就丢凭据
        settings->beginGroup(QLatin1String(kAccountsGroup));
        settings->beginGroup(QString::number(uid));
        settings->setValue(QLatin1String(kUid), QString::number(uid));
        settings->setValue(QLatin1String(kName), name);
        settings->setValue(QLatin1String(kToken), token);
        settings->setValue(QLatin1String(kHost), host);
        settings->setValue(QLatin1String(kPort), port);
        settings->setValue(QLatin1String(kLastLogin), QDateTime::currentMSecsSinceEpoch());
        settings->endGroup();
        settings->endGroup();

        settings->beginGroup(QLatin1String(kSessionGroup));
        settings->setValue(QLatin1String(kLastUid), QString::number(uid));
        settings->remove(QLatin1String(kUid));
        settings->remove(QLatin1String(kName));
        settings->remove(QLatin1String(kToken));
        settings->remove(QLatin1String(kHost));
        settings->remove(QLatin1String(kPort));
        settings->endGroup();
        settings->sync();

        MCLOG_INFO() << "[session] 已把旧的单账号记录迁移成账号卡片: uid =" << uid;
    }

    QList<SessionManager::Account> SessionManager::accounts() const
    {
        migrateLegacy();

        const std::unique_ptr<QSettings> settings = openSettings();
        settings->beginGroup(QLatin1String(kAccountsGroup));
        const QStringList uids = settings->childGroups();

        QList<Account> list;
        list.reserve(uids.size());

        for (const QString& key: uids)
        {
            bool uid_ok = false;
            const std::uint64_t uid = key.toULongLong(&uid_ok);
            if (!uid_ok || uid == 0)
                continue;

            settings->beginGroup(key);
            Account account;
            account.uid = uid;
            account.name = settings->value(QLatin1String(kName)).toString();
            account.token = settings->value(QLatin1String(kToken)).toString();
            account.host = settings->value(QLatin1String(kHost)).toString();
            account.last_login_ms = settings->value(QLatin1String(kLastLogin), 0).toLongLong();

            const int port = settings->value(QLatin1String(kPort), 0).toInt();
            if (port > 0 && port <= 65535)
                account.port = static_cast<std::uint16_t>(port);
            settings->endGroup();

            list.append(account);
        }
        settings->endGroup();

        // 最近用的排最前；时间戳相同（老数据）时按 uid 倒序，保证顺序稳定
        std::sort(list.begin(), list.end(), [](const Account& a, const Account& b)
                  {
                      if (a.last_login_ms != b.last_login_ms)
                          return a.last_login_ms > b.last_login_ms;
                      return a.uid > b.uid;
                  });
        return list;
    }

    std::uint64_t SessionManager::lastUid() const
    {
        migrateLegacy();

        const std::unique_ptr<QSettings> settings = openSettings();
        settings->beginGroup(QLatin1String(kSessionGroup));
        const QString text = settings->value(QLatin1String(kLastUid)).toString();
        settings->endGroup();

        bool ok = false;
        const std::uint64_t uid = text.toULongLong(&ok);
        return ok ? uid : 0;
    }

    bool SessionManager::skipAutoLogin() const
    {
        const std::unique_ptr<QSettings> settings = openSettings();
        settings->beginGroup(QLatin1String(kSessionGroup));
        const bool skip = settings->value(QLatin1String(kSkipAuto), false).toBool();
        settings->endGroup();
        return skip;
    }

    void SessionManager::setSkipAutoLogin(const bool skip)
    {
        const std::unique_ptr<QSettings> settings = openSettings();
        settings->beginGroup(QLatin1String(kSessionGroup));
        if (skip)
            settings->setValue(QLatin1String(kSkipAuto), true);
        else
            settings->remove(QLatin1String(kSkipAuto));
        settings->endGroup();
        settings->sync();
    }

    bool SessionManager::loadAccount(const std::uint64_t uid)
    {
        if (uid == 0)
            return false;

        const QList<Account> list = accounts();
        const auto it = std::find_if(list.cbegin(), list.cend(),
                                     [uid](const Account& a) { return a.uid == uid; });
        if (it == list.cend())
            return false;

        m_uid = it->uid;
        m_name = it->name;
        m_token = it->token;
        m_host = it->host;
        m_port = it->port;

        MCLOG_INFO() << "[session] 已载入账号" << uid
            << "，免密凭据" << (it->hasToken() ? "可用" : "不可用");
        return it->hasToken();
    }

    bool SessionManager::save() const
    {
        if (m_uid == 0 || m_token.isEmpty() || m_host.isEmpty() || m_port == 0)
        {
            MCLOG_WARN() << "[session] 会话字段不全，不写入账号列表: uid =" << m_uid
                << "token 空 =" << m_token.isEmpty()
                << "host =" << m_host << "port =" << m_port;
            return false;
        }

        const std::unique_ptr<QSettings> settings = openSettings();

        settings->beginGroup(QLatin1String(kAccountsGroup));
        settings->beginGroup(QString::number(m_uid));
        // uid 按字符串写：QSettings 的整数是 64 位，但 INI 里读回来变成
        // QVariant(int/longlong) 再拼字符串容易出偏差，统一走字符串最稳。
        settings->setValue(QLatin1String(kUid), QString::number(m_uid));
        settings->setValue(QLatin1String(kName), m_name);
        settings->setValue(QLatin1String(kToken), m_token);
        settings->setValue(QLatin1String(kHost), m_host);
        settings->setValue(QLatin1String(kPort), static_cast<int>(m_port));
        settings->setValue(QLatin1String(kLastLogin), QDateTime::currentMSecsSinceEpoch());
        settings->endGroup();
        settings->endGroup();

        settings->beginGroup(QLatin1String(kSessionGroup));
        settings->setValue(QLatin1String(kLastUid), QString::number(m_uid));
        settings->endGroup();
        settings->sync();

        /*
         * status() 必须查：写失败（目录只读、磁盘满、策略拦）时 QSettings
         * 不抛异常也不打日志，只是什么都不做 —— 不查就会看到
         * "日志说保存成功、实际读回来是空的"。
         */
        const bool ok = settings->status() == QSettings::NoError;
        if (ok)
        {
            MCLOG_INFO() << "[session] 已保存账号凭据: uid =" << m_uid
                << "host =" << m_host << "port =" << m_port
                << "file =" << storagePath();
        }
        else
        {
            MCLOG_WARN() << "[session] 保存账号凭据失败（status ="
                << static_cast<int>(settings->status()) << "），"
                << "本次仍可正常聊天，只是下次要重新登录。file =" << storagePath();
        }
        return ok;
    }

    void SessionManager::forgetAccount(const std::uint64_t uid)
    {
        if (uid == 0)
            return;

        // 先看它是不是"上次登录"，再删 —— 删完就查不到 uid 对应的记录了
        const bool was_last = lastUid() == uid;

        const std::unique_ptr<QSettings> settings = openSettings();
        settings->beginGroup(QLatin1String(kAccountsGroup));
        settings->remove(QString::number(uid));
        settings->endGroup();

        if (was_last)
        {
            // 把标记挪到还在的账号上，免得登录页默认选一个不存在的号
            const QList<Account> rest = accounts();
            settings->beginGroup(QLatin1String(kSessionGroup));
            if (rest.isEmpty())
                settings->remove(QLatin1String(kLastUid));
            else
                settings->setValue(QLatin1String(kLastUid), QString::number(rest.first().uid));
            settings->endGroup();
        }
        settings->sync();

        MCLOG_INFO() << "[session] 已移除账号" << uid;
    }

    void SessionManager::dropToken(const std::uint64_t uid)
    {
        if (uid == 0)
            return;

        const std::unique_ptr<QSettings> settings = openSettings();
        settings->beginGroup(QLatin1String(kAccountsGroup));
        settings->beginGroup(QString::number(uid));
        settings->remove(QLatin1String(kToken));
        settings->remove(QLatin1String(kHost));
        settings->remove(QLatin1String(kPort));
        settings->endGroup();
        settings->endGroup();
        settings->sync();

        MCLOG_INFO() << "[session] 已清除账号" << uid << "的免密凭据（卡片保留）";
    }

    void SessionManager::forgetSaved()
    {
        const std::unique_ptr<QSettings> settings = openSettings();
        settings->remove(QLatin1String(kAccountsGroup));
        settings->beginGroup(QLatin1String(kSessionGroup));
        settings->remove(QLatin1String(kLastUid));
        settings->remove(QLatin1String(kSkipAuto));
        settings->endGroup();
        settings->sync();
        MCLOG_INFO() << "[session] 已清空账号列表";
    }

    QString SessionManager::storagePath()
    {
        const std::unique_ptr<QSettings> settings = openSettings();
        return settings->fileName();
    }

    QString SessionManager::label() const
    {
        if (m_name.isEmpty())
            return QStringLiteral("UID %1").arg(m_uid);
        return QStringLiteral("UID %1 · %2").arg(m_uid).arg(m_name);
    }
} // namespace mychat
