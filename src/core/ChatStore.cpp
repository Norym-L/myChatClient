#include "ChatStore.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QVariant>

#include <algorithm>

namespace
{
/// schema 版本（`PRAGMA user_version`）。加字段 / 改表时 +1，并在 ensureSchema 里补迁移。
constexpr int kSchemaVersion = 1;

/// 进程被杀时还在飞的消息，读回来一律算失败 —— 回执早就没了，别让它永远转圈
constexpr char kStateSending[] = "sending";
constexpr char kStateFailed[] = "failed";
} // namespace

ChatStore::ChatStore() = default;

ChatStore::~ChatStore()
{
    close();
}

QString ChatStore::dataDirectory()
{
    // 自检会把它指到临时目录，避免污染真实用户数据
    if (const QString override_dir = qEnvironmentVariable("MYCHAT_DATA_DIR");
        !override_dir.isEmpty())
    {
        return override_dir;
    }

    /*
     * 默认放**可执行文件目录下的 data/**（便携式）：
     * 和 config.ini 同一个思路 —— 绿色包拷走就能带走自己的聊天记录。
     * ⚠ 装在 Program Files 之类只读位置时这里会写不进去，
     *   open() 会退回系统数据目录（见 fallbackDataDirectory）。
     */
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("data"));
}

QString ChatStore::fallbackDataDirectory()
{
    // Windows: %APPDATA%/MyChat —— 只在 exe 目录不可写时才用到
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
}

QString ChatStore::databasePathFor(const std::uint64_t uid)
{
    return QDir(dataDirectory()).filePath(QStringLiteral("chat_%1.db").arg(uid));
}

bool ChatStore::open(const std::uint64_t uid)
{
    if (openDatabase(databasePathFor(uid)))
        return true;

    /*
     * 兜底：exe 目录可能只读（Program Files / 移动介质写保护）。
     * 历史比"路径好看"重要 —— 退到用户数据目录，至少不丢消息。
     */
    const QString fallback =
        QDir(fallbackDataDirectory()).filePath(QStringLiteral("chat_%1.db").arg(uid));
    if (fallback == databasePathFor(uid))
        return false;

    qWarning() << "[store] exe 目录下的数据目录不可写，改用:" << fallback;
    return openDatabase(fallback);
}

bool ChatStore::openAt(const QString& filePath)
{
    return openDatabase(filePath);
}

bool ChatStore::openDatabase(const QString& filePath)
{
    close();

    if (filePath.isEmpty())
    {
        qWarning() << "[store] 库路径为空，本地记录不可用";
        return false;
    }

    const QFileInfo info(filePath);
    if (!QDir().mkpath(info.absolutePath()))
    {
        qWarning() << "[store] 建不了数据目录:" << info.absolutePath();
        return false;
    }

    // 每个实例一个连接名：自检会同时开多个库，共用一个名字会互相顶掉
    static int connection_seq = 0;
    m_connection = QStringLiteral("mychat_store_%1").arg(++connection_seq);

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
    m_db.setDatabaseName(filePath);
    if (!m_db.open())
    {
        // 最常见的原因：windeployqt 没把 sqldrivers/qsqlite.dll 带出来
        qCritical() << "[store] 打不开本地聊天库:" << filePath << m_db.lastError().text();
        close();
        return false;
    }

    m_path = filePath;

    QSqlQuery pragma(m_db);
    // WAL：读写不互相阻塞，进程被强杀也不容易把库写坏
    pragma.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    pragma.exec(QStringLiteral("PRAGMA synchronous=NORMAL"));

    if (!ensureSchema())
    {
        close();
        return false;
    }


    qInfo() << "[store] 本地聊天库已打开:" << m_path;
    return true;
}

void ChatStore::close()
{
    if (!m_db.isValid())
    {
        m_path.clear();
        return;
    }

    const QString name = m_connection;
    if (m_db.isOpen())
        m_db.close();

    // 必须先让 QSqlDatabase 释放掉这个连接名，否则 removeDatabase 会告警"仍在使用"
    m_db = QSqlDatabase();
    m_connection.clear();
    m_path.clear();

    if (!name.isEmpty())
        QSqlDatabase::removeDatabase(name);
}

bool ChatStore::isOpen() const
{
    return m_db.isValid() && m_db.isOpen();
}

bool ChatStore::ensureSchema()
{
    const char* statements[] = {
        /*
         * ⚠ 字符串列**不要随便加 NOT NULL**：Qt 把空 QString 绑定成 NULL，
         *   而 `INSERT OR IGNORE` 遇到 NOT NULL 违约会**静默丢弃整行**，
         *   表现成"自己发的消息一条都存不进去"（踩过两次：server_id / from_uid）。
         *   这里只对真正必填、且由本类保证非空的 peer_uid 保留 NOT NULL。
         */
        R"(CREATE TABLE IF NOT EXISTS messages(
               id        INTEGER PRIMARY KEY AUTOINCREMENT,
               peer_uid  TEXT    NOT NULL,
               from_self INTEGER NOT NULL DEFAULT 0,
               from_uid  TEXT,
               body      TEXT,
               type      INTEGER NOT NULL DEFAULT 1,
               created_ms INTEGER NOT NULL DEFAULT 0,
               server_id TEXT,
               state     TEXT))",
        R"(CREATE INDEX IF NOT EXISTS idx_messages_peer ON messages(peer_uid, created_ms))",
        /*
         * 去重索引：**只对"有服务端编号"的消息生效**（部分索引）。
         *
         * 不能写列级 UNIQUE：自己发的消息没有 server_id，空值不该参与唯一性；
         * 而且 Qt 绑空 QString 时写成 NULL 还是空串并不稳定，
         * `WHERE server_id <> ''` 把两种空值整类排除，与绑定方式无关。
         */
        R"(CREATE UNIQUE INDEX IF NOT EXISTS idx_messages_server
               ON messages(server_id) WHERE server_id <> '')",
        R"(CREATE TABLE IF NOT EXISTS conversations(
               peer_uid   TEXT PRIMARY KEY,
               last_text  TEXT,
               last_time  TEXT,
               last_ms    INTEGER NOT NULL DEFAULT 0,
               unread     INTEGER NOT NULL DEFAULT 0,
               updated_ms INTEGER NOT NULL DEFAULT 0))",
    };

    QSqlQuery query(m_db);
    for (const char* statement: statements)
    {
        if (!query.exec(QString::fromLatin1(statement)))
        {
            qCritical() << "[store] 建表失败:" << query.lastError().text();
            return false;
        }
    }

    QSqlQuery version_query(m_db);
    if (version_query.exec(QStringLiteral("PRAGMA user_version")) && version_query.next())
    {
        const int version = version_query.value(0).toInt();
        if (version < kSchemaVersion)
        {
            // 目前只有 v1；以后加字段时在这里按 version 逐级迁移
            version_query.exec(QStringLiteral("PRAGMA user_version=%1").arg(kSchemaVersion));
        }
    }
    return true;
}

QList<ChatStore::Conversation> ChatStore::conversations(const int limit) const
{
    QList<Conversation> list;
    if (!isOpen())
        return list;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT peer_uid, last_text, last_time, last_ms, unread, updated_ms"
        " FROM conversations ORDER BY updated_ms DESC LIMIT ?"));
    query.addBindValue(limit);
    if (!query.exec())
    {
        qWarning() << "[store] 读会话列表失败:" << query.lastError().text();
        return list;
    }

    while (query.next())
    {
        Conversation row;
        row.peerUid = query.value(0).toString();
        row.lastText = query.value(1).toString();
        row.lastTime = query.value(2).toString();
        row.lastMs = query.value(3).toLongLong();
        row.unread = query.value(4).toInt();
        row.updatedMs = query.value(5).toLongLong();
        if (!row.peerUid.isEmpty())
            list.append(row);
    }
    return list;
}

bool ChatStore::saveConversation(const Conversation& row)
{
    if (!isOpen() || row.peerUid.isEmpty())
        return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "INSERT INTO conversations(peer_uid, last_text, last_time, last_ms, unread, updated_ms)"
        " VALUES(?, ?, ?, ?, ?, ?)"
        " ON CONFLICT(peer_uid) DO UPDATE SET"
        "   last_text=excluded.last_text, last_time=excluded.last_time,"
        "   last_ms=excluded.last_ms, unread=excluded.unread, updated_ms=excluded.updated_ms"));
    query.addBindValue(row.peerUid);
    query.addBindValue(row.lastText);
    query.addBindValue(row.lastTime);
    query.addBindValue(row.lastMs);
    query.addBindValue(row.unread);
    query.addBindValue(row.updatedMs == 0 ? QDateTime::currentMSecsSinceEpoch() : row.updatedMs);

    if (!query.exec())
    {
        qWarning() << "[store] 写会话失败:" << query.lastError().text();
        return false;
    }
    return true;
}

bool ChatStore::markConversationRead(const QString& peerUid)
{
    if (!isOpen() || peerUid.isEmpty())
        return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE conversations SET unread=0 WHERE peer_uid=?"));
    query.addBindValue(peerUid);
    return query.exec();
}

QList<ChatStore::Message> ChatStore::recentMessages(const QString& peerUid, const int limit) const
{
    QList<Message> list;
    if (!isOpen() || peerUid.isEmpty())
        return list;

    QSqlQuery query(m_db);
    // 先按时间倒序取最近 N 条，再翻回正序 —— 直接正序 LIMIT 会拿到最老的 N 条
    query.prepare(QStringLiteral(
        "SELECT id, from_self, from_uid, body, type, created_ms, server_id, state"
        " FROM messages WHERE peer_uid=? ORDER BY created_ms DESC, id DESC LIMIT ?"));
    query.addBindValue(peerUid);
    query.addBindValue(limit);
    if (!query.exec())
    {
        qWarning() << "[store] 读消息失败:" << query.lastError().text();
        return list;
    }

    while (query.next())
    {
        Message message;
        message.id = query.value(0).toLongLong();
        message.peerUid = peerUid;
        message.fromSelf = query.value(1).toInt() != 0;
        message.fromUid = query.value(2).toString();
        message.body = query.value(3).toString();
        message.type = query.value(4).toInt();
        message.createdMs = query.value(5).toLongLong();
        message.serverId = query.value(6).toString();
        message.state = query.value(7).toString();

        // 上次进程在"等回执"时被杀掉 → 那条消息不可能再有结果，标失败而不是永远转圈
        if (message.state == QLatin1String(kStateSending))
            message.state = QString::fromLatin1(kStateFailed);

        list.append(message);
    }

    std::reverse(list.begin(), list.end());
    return list;
}

bool ChatStore::lastMessage(const QString& peerUid, Message& out) const
{
    if (!isOpen() || peerUid.isEmpty())
        return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral(
        "SELECT id, from_self, from_uid, body, type, created_ms, server_id, state"
        " FROM messages WHERE peer_uid=? ORDER BY created_ms DESC, id DESC LIMIT 1"));
    query.addBindValue(peerUid);
    if (!query.exec() || !query.next())
        return false;

    out = Message{};
    out.id = query.value(0).toLongLong();
    out.peerUid = peerUid;
    out.fromSelf = query.value(1).toInt() != 0;
    out.fromUid = query.value(2).toString();
    out.body = query.value(3).toString();
    out.type = query.value(4).toInt();
    out.createdMs = query.value(5).toLongLong();
    out.serverId = query.value(6).toString();
    out.state = query.value(7).toString();
    return true;
}

int ChatStore::deleteMessages(const QList<qint64>& ids)
{
    if (!isOpen() || ids.isEmpty())
        return 0;

    /*
     * 一条 SQL 删一批（占位符按条数生成）。用事务包住：
     * 删到一半失败时不该留下"删了几条"的中间状态。
     */
    QStringList placeholders;
    placeholders.reserve(ids.size());
    for (int i = 0; i < ids.size(); ++i)
        placeholders.append(QStringLiteral("?"));

    if (!m_db.transaction())
    {
        qWarning() << "[store] 开启事务失败:" << m_db.lastError().text();
        return -1;
    }

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("DELETE FROM messages WHERE id IN (%1)")
                      .arg(placeholders.join(QLatin1Char(','))));
    for (const qint64 id: ids)
        query.addBindValue(id);

    if (!query.exec())
    {
        qWarning() << "[store] 删除消息失败:" << query.lastError().text();
        m_db.rollback();
        return -1;
    }

    const int removed = query.numRowsAffected();
    if (!m_db.commit())
    {
        qWarning() << "[store] 提交删除失败:" << m_db.lastError().text();
        m_db.rollback();
        return -1;
    }

    return removed;
}

bool ChatStore::clearConversation(const QString& peerUid)
{
    if (!isOpen() || peerUid.isEmpty())
        return false;

    if (!m_db.transaction())
    {
        qWarning() << "[store] 开启事务失败:" << m_db.lastError().text();
        return false;
    }

    QSqlQuery delete_messages(m_db);
    delete_messages.prepare(QStringLiteral("DELETE FROM messages WHERE peer_uid=?"));
    delete_messages.addBindValue(peerUid);

    QSqlQuery delete_conversation(m_db);
    delete_conversation.prepare(QStringLiteral("DELETE FROM conversations WHERE peer_uid=?"));
    delete_conversation.addBindValue(peerUid);

    if (!delete_messages.exec() || !delete_conversation.exec())
    {
        qWarning() << "[store] 清空会话失败:" << delete_messages.lastError().text();
        m_db.rollback();
        return false;
    }

    if (!m_db.commit())
    {
        qWarning() << "[store] 提交清空失败:" << m_db.lastError().text();
        m_db.rollback();
        return false;
    }

    return true;
}

ChatStore::InsertResult ChatStore::insertMessage(Message& message)
{
    // peer_uid 是唯一保留的 NOT NULL 列：空 peer 说明调用方有 bug，别静默写进去
    if (!isOpen() || message.peerUid.isEmpty())
        return InsertResult::Failed;

    QSqlQuery query(m_db);
    // INSERT OR IGNORE + server_id UNIQUE：PEL 重投（含跨进程）在这里被挡住
    query.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO messages"
        " (peer_uid, from_self, from_uid, body, type, created_ms, server_id, state)"
        " VALUES(?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(message.peerUid);
    query.addBindValue(message.fromSelf ? 1 : 0);
    query.addBindValue(message.fromUid);
    query.addBindValue(message.body);
    query.addBindValue(message.type);
    query.addBindValue(message.createdMs);
    // 自己发的消息没有服务端编号 → 存 NULL（唯一索引里用 WHERE 排除了空值）
    query.addBindValue(message.serverId.isEmpty() ? QVariant() : QVariant(message.serverId));
    query.addBindValue(message.state.isEmpty() ? QStringLiteral("sent") : message.state);

    if (!query.exec())
    {
        qWarning() << "[store] 写消息失败:" << query.lastError().text();
        return InsertResult::Failed;
    }

    if (query.numRowsAffected() == 0)
    {
        // 行被 IGNORE 掉了：要么 server_id 撞车（PEL 重投，正常），
        // 要么是别的约束（真 bug）—— 留一条能查的日志
        qDebug() << "[store] 插入被忽略: server_id =" << message.serverId
            << "peer =" << message.peerUid;
        return InsertResult::Duplicate;
    }

    message.id = query.lastInsertId().toLongLong();
    return InsertResult::Inserted;
}

bool ChatStore::updateMessageState(const qint64 id, const QString& state)
{
    if (!isOpen() || id <= 0)
        return false;

    QSqlQuery query(m_db);
    query.prepare(QStringLiteral("UPDATE messages SET state=? WHERE id=?"));
    query.addBindValue(state);
    query.addBindValue(id);
    if (!query.exec())
    {
        qWarning() << "[store] 更新消息状态失败:" << query.lastError().text();
        return false;
    }
    return true;
}
