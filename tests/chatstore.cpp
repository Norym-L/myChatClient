// 本地聊天记录（SQLite）自检 —— 不需要服务端、不需要 GUI。
//
// 为什么单独测：服务端确认即删除（XACKDEL），本地库是唯一的历史来源，
// 这条链路的正确性没法靠"看一眼界面"判断。这里钉住四件事：
//
//   1. 重开库还能读回来（真落盘，不是内存缓存）；
//   2. 同一个 `server_id` 只能进一条（PEL 重投 / 跨进程重复的兜底）；
//   3. 自己发的消息（没有 server_id）不受 UNIQUE 限制，可以有多条；
//   4. 进程被杀时还在 `sending` 的消息，读回来一律是 `failed`（回执不可能再有了）。
//
// 用法：
//   qtClientChatStoreTest
//
// 退出码：0 全部通过；3 有断言失败。

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <cstdio>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "ChatStore.h"

namespace
{
int fail_count = 0;

void writeLine(FILE* stream, const QString& text)
{
    const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
    std::fflush(stream);
}

void check(const bool ok, const QString& what)
{
    if (ok)
    {
        writeLine(stdout, QStringLiteral("[OK] %1").arg(what));
        return;
    }

    writeLine(stderr, QStringLiteral("[FAIL] %1").arg(what));
    ++fail_count;
}

QString tempDir()
{
    /*
     * 放在 exe 旁边而不是 %TEMP%：受限环境（沙箱 / 只读临时目录）里
     * %TEMP% 下的写入会静默失败，表现成"打开本地库"直接报错 ——
     * 看起来像 ChatStore 坏了，其实是环境不让写。构建目录一定可写。
     */
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("test-data/mychat-store-test"));
}


ChatStore::Message makeMessage(const QString& peer, const bool from_self, const QString& body,
                               const qint64 created_ms, const QString& server_id,
                               const QString& state)
{
    ChatStore::Message message;
    message.peerUid = peer;
    message.fromSelf = from_self;
    message.fromUid = from_self ? QString() : peer;
    message.body = body;
    message.type = 1;
    message.createdMs = created_ms;
    message.serverId = server_id;
    message.state = state;
    return message;
}
} // namespace

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MyChat"));
    QCoreApplication::setOrganizationName(QStringLiteral("MyChat"));

    const QString dir = tempDir();
    // 用全新的临时目录：上一轮残留的**旧 schema** 库会让"重开还能读回"这条测歪
    // （踩过：旧库的列级 UNIQUE 还在，空 server_id 会被 INSERT OR IGNORE 静默丢掉）
    QDir(dir).removeRecursively();
    const QString db_path = QDir(dir).filePath(QStringLiteral("chat_10001.db"));
    writeLine(stdout, QStringLiteral("[setup] 测试库: %1").arg(db_path));

    const QString peer = QStringLiteral("666674");
    const qint64 base_ms = 1734567890000;

    // ── 第一段：写入 ────────────────────────────────────────────────
    {
        ChatStore store;
        check(store.openAt(db_path), QStringLiteral("打开本地库"));
        check(store.isOpen(), QStringLiteral("库处于打开状态"));
        check(QFileInfo::exists(db_path), QStringLiteral("库文件已创建"));

        ChatStore::Message incoming =
            makeMessage(peer, false, QStringLiteral("在吗？"), base_ms,
                        QStringLiteral("1734567890000-0"), QStringLiteral("sent"));
        check(store.insertMessage(incoming) == ChatStore::InsertResult::Inserted,
              QStringLiteral("收到的新消息插入成功"));
        check(incoming.id > 0, QStringLiteral("插入后回填了自增 id"));

        ChatStore::Message duplicate =
            makeMessage(peer, false, QStringLiteral("在吗？"), base_ms,
                        QStringLiteral("1734567890000-0"), QStringLiteral("sent"));
        check(store.insertMessage(duplicate) == ChatStore::InsertResult::Duplicate,
              QStringLiteral("同一个 server_id 再插一次 → Duplicate（PEL 重投被挡住）"));

        ChatStore::Message mine1 =
            makeMessage(peer, true, QStringLiteral("在"), base_ms + 1000, QString(), QStringLiteral("sent"));
        ChatStore::Message mine2 =
            makeMessage(peer, true, QStringLiteral("刚看到"), base_ms + 2000, QString(), QStringLiteral("sent"));
        const ChatStore::InsertResult r1 = store.insertMessage(mine1);
        const ChatStore::InsertResult r2 = store.insertMessage(mine2);
        check(r1 == ChatStore::InsertResult::Inserted
                  && r2 == ChatStore::InsertResult::Inserted,
              QStringLiteral("自己发的消息没有 server_id，可以多条（不参与唯一索引）"));

        ChatStore::Message stuck =
            makeMessage(peer, true, QStringLiteral("进程被杀时在飞"), base_ms + 3000, QString(),
                        QStringLiteral("sending"));
        check(store.insertMessage(stuck) == ChatStore::InsertResult::Inserted,
              QStringLiteral("写入一条 sending 状态的消息（模拟进程被杀）"));

        check(store.updateMessageState(mine1.id, QStringLiteral("failed")),
              QStringLiteral("能把某条消息的状态改成 failed"));

        ChatStore::Conversation row;
        row.peerUid = peer;
        row.lastText = QStringLiteral("进程被杀时在飞");
        row.lastTime = QStringLiteral("18:04");
        row.unread = 3;
        check(store.saveConversation(row), QStringLiteral("会话摘要写入成功"));

        const QList<ChatStore::Message> messages = store.recentMessages(peer);
        check(messages.size() == 4, QStringLiteral("会话里共 4 条（重复那条没进去）"));
        check(!messages.isEmpty() && messages.last().body == QStringLiteral("进程被杀时在飞"),
              QStringLiteral("按时间正序返回，最后一条是最新的"));
    }

    // ── 第二段：重开库 ──────────────────────────────────────────────
    {
        ChatStore store;
        check(store.openAt(db_path), QStringLiteral("重新打开同一个库"));

        const QList<ChatStore::Message> messages = store.recentMessages(peer);
        check(messages.size() == 4, QStringLiteral("重开后消息还在（真落盘了）"));

        bool stuck_is_failed = false;
        bool mine_is_failed = false;
        for (const ChatStore::Message& message: messages)
        {
            if (message.body == QStringLiteral("进程被杀时在飞"))
                stuck_is_failed = message.state == QStringLiteral("failed");
            if (message.body == QStringLiteral("在"))
                mine_is_failed = message.state == QStringLiteral("failed");
        }
        check(stuck_is_failed, QStringLiteral("sending 读回来变成 failed（不会再转圈）"));
        check(mine_is_failed, QStringLiteral("手动改过的 failed 状态也保留了"));

        const QList<ChatStore::Conversation> conversations = store.conversations();
        check(conversations.size() == 1, QStringLiteral("会话列表读回来 1 条"));
        check(!conversations.isEmpty() && conversations.first().unread == 3,
              QStringLiteral("未读数持久化（重启后红点还在）"));

        check(store.markConversationRead(peer), QStringLiteral("标记已读"));
        const QList<ChatStore::Conversation> after = store.conversations();
        check(!after.isEmpty() && after.first().unread == 0, QStringLiteral("已读也落盘了"));
    }

    // ── 第三段：删除 / 清空 ────────────────────────────────────────
    {
        ChatStore store;
        check(store.openAt(db_path), QStringLiteral("再次打开库，准备删除"));

        const QList<ChatStore::Message> before = store.recentMessages(peer);
        check(before.size() == 4, QStringLiteral("删除前 4 条"));

        const int removed = store.deleteMessages({before.at(0).id, before.at(1).id});
        check(removed == 2, QStringLiteral("deleteMessages 返回实际删除条数（2）"));
        check(store.recentMessages(peer).size() == 2, QStringLiteral("删完剩 2 条"));

        check(store.deleteMessages({before.at(0).id}) == 0,
              QStringLiteral("重复删除同一条返回 0（不报错）"));

        ChatStore::Message last;
        check(store.lastMessage(peer, last) && last.id == before.last().id,
              QStringLiteral("lastMessage 取到的是剩下那批的最后一条"));

        check(store.clearConversation(peer), QStringLiteral("清空会话"));
        check(store.recentMessages(peer).isEmpty(), QStringLiteral("清空后没有消息了"));
        check(store.conversations().isEmpty(), QStringLiteral("清空后会话摘要行也删了"));
    }

    // ── 第四段：清掉测试数据 ────────────────────────────────────────
    if (qEnvironmentVariableIsEmpty("MYCHAT_STORE_KEEP"))
        QDir(dir).removeRecursively();
    else
        writeLine(stdout, QStringLiteral("[debug] 保留测试库用于排查: %1").arg(db_path));

    writeLine(stdout, fail_count == 0
                  ? QStringLiteral("\n[PASS] 本地记录链路全部通过")
                  : QStringLiteral("\n[FAIL] 共 %1 项失败").arg(fail_count));
    return fail_count == 0 ? 0 : 3;
}
