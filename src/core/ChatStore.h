#pragma once

#include <QList>
#include <QSqlDatabase>
#include <QString>

#include <cstdint>

/**
 * 本地聊天记录（SQLite）。
 *
 * 为什么必须有：服务端确认（1025）走 `XACKDEL`，**确认即删除**，而且没有历史接口。
 * 客户端不落盘，消息确认完就永远没了 —— 本地库是唯一的历史来源。
 *
 * 落盘顺序（很重要）：收到 1024 → **先写库 → 再 ACK**。
 * 反过来的话，进程在两步之间崩掉，那条消息就既不在服务端（已删）也不在本地。
 *
 * 库文件：`<exe 目录>/data/chat_<uid>.db` —— **按账号分库**，
 * 换账号登录历史天然隔离，不用在每张表上再挂一个 owner_uid。
 * 便携式：整个程序目录拷走，聊天记录跟着走（和 config.ini 同一个思路）。
 * 环境变量 `MYCHAT_DATA_DIR` 可覆盖目录（自检用，避免污染真实数据）；
 * exe 目录不可写（Program Files / 写保护介质）时自动退回系统数据目录。
 *
 * 表结构（schema 版本见 `PRAGMA user_version`）：
 *   messages(id INTEGER PK AUTOINCREMENT, peer_uid, from_self, from_uid, body,
 *            type, created_ms, server_id TEXT NOT NULL DEFAULT '', state)
 *     + 部分唯一索引 `UNIQUE(server_id) WHERE server_id <> ''`
 *   conversations(peer_uid PK, last_text, last_time, last_ms, unread, updated_ms, kind)
 *   groups(gid PK, name, icon, announcement, role, updated_ms)
 *
 * ⚠ 群会话的 `peer_uid` 存的是 **`g<gid>`**（如 `g20001`），不是裸群号：
 *   群 id 和用户 uid 都是数字，来自两张表、可能撞号（群 20001 vs 用户 20001），
 *   不加前缀就会串会话。`kind` 列区分 user / group。
 *
 * ⚠ `server_id` 的唯一索引是**跨会话去重的兜底**：服务端 PEL 会重投未确认的消息，
 *   进程重启后内存里去重表已经没了，只有数据库能挡住重复插入。
 *   用部分索引而不是列级 UNIQUE —— 自己发的消息没有服务端编号（空串），
 *   不应该参与唯一性约束。
 */
class ChatStore
{
public:
    /// 一条消息（字段与 QML 里的气泡一一对应）
    struct Message
    {
        /// 数据库自增 id；同时作为界面用的消息 id（`markMessageState` 按它改状态）
        qint64 id = 0;
        QString peerUid;
        bool fromSelf = false;
        QString fromUid;
        QString body;
        int type = 1;
        qint64 createdMs = 0;
        /// 服务端消息编号（Stream ID）；**自己发的消息没有**（服务端不回传）
        QString serverId;
        /// sending / sent / failed
        QString state;
    };

    /// 会话摘要（列表用）
    struct Conversation
    {
        /// 会话键：单聊是 uid，群聊是 `g<gid>`（见文件头说明）
        QString peerUid;
        QString lastText;
        QString lastTime;
        qint64 lastMs = 0;
        int unread = 0;
        qint64 updatedMs = 0;
        /// "user"（默认）/ "group"：重启后回填会话列表时靠它区分是不是群
        QString kind = QStringLiteral("user");
    };

    /**
     * 群资料（`groups` 表）。
     *
     * 为什么必须本地存：服务端**没有群资料查询接口**（`api-group.md` §11.8），
     * 群名 / 头像 / 公告只在创建（1033）时提交过一次，之后再也要不回来。
     * 本机这份是唯一来源；换设备 / 清库后只能显示「群 <gid>」。
     */
    struct Group
    {
        QString gid;
        QString name;
        QString icon;
        QString announcement;
        /// 我的角色：2 群主 / 1 管理员 / 0 成员（1042 才知道；创建群的人固定是 2）
        int role = 0;
        qint64 updatedMs = 0;
    };

    enum class InsertResult
    {
        Inserted,
        /// `server_id` 已存在（PEL 重投 / 跨进程重复）→ 不该再显示，但仍要 ACK
        Duplicate,
        /// 没开库或写失败 → 调用方应退化成纯内存，不能因此丢消息
        Failed,
    };

    ChatStore();
    ~ChatStore();

    ChatStore(const ChatStore&) = delete;
    ChatStore& operator=(const ChatStore&) = delete;

    /// 打开某个账号的库（目录由 AppData / MYCHAT_DATA_DIR 决定）
    bool open(std::uint64_t uid);
    /// 打开指定路径（自检用）
    bool openAt(const QString& filePath);
    void close();

    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString path() const { return m_path; }

    /// 会话列表（按 updated_ms 倒序）
    [[nodiscard]] QList<Conversation> conversations(int limit = 200) const;
    bool saveConversation(const Conversation& row);
    bool markConversationRead(const QString& peerUid);

    /// 全部群资料（重启后回填群会话名用）
    [[nodiscard]] QList<Group> groups() const;
    /// 新增 / 更新一个群（按 gid upsert）
    bool saveGroup(const Group& row);
    /// 删掉一个群（解散 / 被移出后）
    bool removeGroup(const QString& gid);

    /// 某个会话最近 \a limit 条消息（按时间正序返回）
    [[nodiscard]] QList<Message> recentMessages(const QString& peerUid, int limit = 200) const;

    /**
     * 某个会话里比 \a beforeId 更早的一批消息（按时间正序返回）。
     *
     * 聊天记录往上翻页用：id 是自增主键，同一个会话里 id 越大越新，
     * 所以"比这条更早"就是 id < beforeId —— 比再传一个时间戳去比更稳，
     * 同一毫秒里发的几条也不会漏掉或者重复。
     *
     * @return 最多 \a limit 条；不足 limit 说明已经翻到头了
     */
    [[nodiscard]] QList<Message> messagesBefore(const QString& peerUid, qint64 beforeId,
                                                int limit = 30) const;

    /// 某个会话的最后一条消息（删完消息后重算摘要用）；没有时返回 false
    [[nodiscard]] bool lastMessage(const QString& peerUid, Message& out) const;

    /**
     * 删除若干条消息（**只删本地**）。
     *
     * @return 实际删掉的行数；没开库或 SQL 失败返回 -1
     *
     * \note 服务端没有"删除消息"接口 —— 消息在确认（1025）时就已经被 XACKDEL 删掉了，
     *       这里删的只是本机这份记录，对方那边完全不受影响。
     */
    int deleteMessages(const QList<qint64>& ids);

    /// 清空某个会话（消息 + 会话摘要行）；只影响本机
    bool clearConversation(const QString& peerUid);

    /**
     * 插入一条消息。
     *
     * @param[in,out] message 成功（Inserted）时回填 \l Message::id
     */
    InsertResult insertMessage(Message& message);

    bool updateMessageState(qint64 id, const QString& state);

    /// 账号数据目录（`MYCHAT_DATA_DIR` 优先，其次 **exe 目录下的 data/**）
    [[nodiscard]] static QString dataDirectory();
    /// 兜底数据目录：exe 目录不可写时用系统数据目录（AppData）
    [[nodiscard]] static QString fallbackDataDirectory();
    /// `chat_<uid>.db` 的完整路径
    [[nodiscard]] static QString databasePathFor(std::uint64_t uid);

private:
    bool openDatabase(const QString& filePath);
    bool ensureSchema();
    /// \a table 里有没有 \a column（schema 迁移用；`PRAGMA table_info`）
    [[nodiscard]] bool hasColumn(const QString& table, const QString& column) const;

    QSqlDatabase m_db;
    QString m_path;
    /// 每个实例一个连接名：自检里会同时开多个库，共用一个名字会互相顶掉
    QString m_connection;
};
