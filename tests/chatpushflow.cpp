// 直推消息链路自检（1047 推送 → 落地，1024 新字段兼容）
// —— 自带假 ChatServer，不需要真实服务端。
//
// 背景（ServerDocs/design-message-push.md，2026-10-24 实验分支）：
//   接收方在线时，服务端调度器直接推 **1047 完整消息体**
//   （`group_id` / `from_uid` / `type` / `body` / `time`），
//   聊天消息不再推 1022、也**不再写** `offlinestream_<uid>`；
//   `1024` 的 `msgs[]` 同时改了字段：取消布尔 `group`、`group_id` 恒出现
//   （单聊 `"0"`）、`id` 改名 `time`。
//
// 这条自检钉住的是客户端的取舍：
//   1. 1047 到了一条就直接上屏：**不发 1023 拉取、不发 1025 确认**；
//   2. 同一条（同一个 `time`）推两次只显示一次；
//   3. 单聊 / 群聊 / 陌生人 / 自己的群消息回显 / 系统消息 五种归属都对；
//   4. 1047 的 `group_id = "0"` 是单聊，不是"群 0"；
//   5. 1024 的新格式（`time` + `group_id`）与旧格式（`id` + `group`）都能解析，
//      确认（1025）回传的仍是同一个 Stream 编号；
//   6. 群消息"不在群里"的错误码从 1022 改成 1023 后，提示照旧。
//
// 用法（无需服务端、无需网络）：
//   qtClientChatPushTest
//
// 退出码：0 全部通过；3 某一步断言失败；4 整体超时；5 起不了本地监听。

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <cstdio>
#include <functional>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "ChatController.h"
#include "ChatSocket.h"
#include "ChatStore.h"
#include "logger.h"

namespace
{
constexpr int kTickMs = 150;          // driver 每一步的间隔（够 socket 往返 + 槽执行）
constexpr int kOverallTimeoutMs = 40000;

constexpr std::uint16_t kLoginReq = 1001;
constexpr std::uint16_t kLoginRsp = 1002;
constexpr std::uint16_t kTextChatMsgReq = 1013;
constexpr std::uint16_t kTextChatMsgRsp = 1014;
constexpr std::uint16_t kSyncChatMessageReq = 1023;
constexpr std::uint16_t kSyncChatMessageRsp = 1024;
constexpr std::uint16_t kAckChatMessageReq = 1025;
constexpr std::uint16_t kAckChatMessageRsp = 1026;
constexpr std::uint16_t kChatMessagePush = 1047;

const char* kSelfUid = "666672";
/// 当前会话的对方
const char* kPeerUid = "666674";
/// 没建过会话的陌生人
const char* kStrangerUid = "666676";

const QString kSingleTime1 = QStringLiteral("1734567890900-0");
const QString kGroupTime = QStringLiteral("1734567890910-0");
const QString kSelfEchoTime = QStringLiteral("1734567890911-0");
const QString kSystemTime = QStringLiteral("1734567890912-0");
const QString kStrangerTime = QStringLiteral("1734567890913-0");

void writeLine(FILE* stream, const QString& text)
{
    const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
    std::fflush(stream);
}

/// 假 ChatServer：登录 / 发送回执 / 拉取 / 确认，四类请求够用
class FakeServer
{
public:
    FakeServer()
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, [this]()
                             {
                                 m_peer = m_server.nextPendingConnection();
                                 if (m_peer == nullptr)
                                     return;

                                 QObject::connect(m_peer, &QTcpSocket::readyRead, [this]()
                                                      {
                                                          m_buffer.append(m_peer->readAll());
                                                          dispatch();
                                                      });
                             });
    }

    [[nodiscard]] bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }

    std::function<void(std::uint16_t, const QByteArray&)> onFrame;

    void reply(const std::uint16_t id, const QByteArray& body)
    {
        if (m_peer == nullptr)
            return;

        QByteArray frame;
        frame.reserve(4 + body.size());
        frame.append(static_cast<char>((id >> 8) & 0xFF));
        frame.append(static_cast<char>(id & 0xFF));
        const auto len = static_cast<std::uint16_t>(body.size());
        frame.append(static_cast<char>((len >> 8) & 0xFF));
        frame.append(static_cast<char>(len & 0xFF));
        frame.append(body);

        m_peer->write(frame);
        m_peer->flush();
    }

    void replyJson(const std::uint16_t id, const QJsonObject& body)
    {
        reply(id, QJsonDocument(body).toJson(QJsonDocument::Compact));
    }

private:
    void dispatch()
    {
        for (;;)
        {
            if (m_buffer.size() < 4)
                return;

            const auto* head = reinterpret_cast<const unsigned char*>(m_buffer.constData());
            const auto id = static_cast<std::uint16_t>((head[0] << 8) | head[1]);
            const auto len = static_cast<std::uint16_t>((head[2] << 8) | head[3]);

            if (m_buffer.size() < 4 + len)
                return;

            const QByteArray body = m_buffer.mid(4, len);
            m_buffer.remove(0, 4 + len);

            if (onFrame)
                onFrame(id, body);
        }
    }

    QTcpServer m_server;
    QTcpSocket* m_peer = nullptr;
    QByteArray m_buffer;
};

/// 1047 推送体
QJsonObject makePush(const char* fromUid, const QString& gid, const QString& body,
                     const QString& time)
{
    QJsonObject push;
    // ⚠ group_id 恒出现：单聊是字符串 "0"（不是 ""、不是省略、不是 false）
    push.insert(QStringLiteral("group_id"), gid);
    push.insert(QStringLiteral("from_uid"), QString::fromLatin1(fromUid));
    push.insert(QStringLiteral("type"), 1);
    push.insert(QStringLiteral("body"), body);
    push.insert(QStringLiteral("time"), time);
    return push;
}

/// 1024 新格式的一条（2026-10-24：time + group_id，无 group）
QJsonObject makeMsgNew(const char* fromUid, const QString& gid, const QString& body,
                       const QString& time)
{
    return makePush(fromUid, gid, body, time);
}

/// 1024 旧格式的一条（2026-10-23：id + 布尔 group，group_id 只有群消息才有）
QJsonObject makeMsgLegacy(const char* fromUid, const bool group, const QString& gid,
                          const QString& body, const QString& id)
{
    QJsonObject item;
    item.insert(QStringLiteral("type"), 1);
    item.insert(QStringLiteral("from_uid"), QString::fromLatin1(fromUid));
    item.insert(QStringLiteral("body"), body);
    item.insert(QStringLiteral("id"), id);
    item.insert(QStringLiteral("group"), group);
    if (!gid.isEmpty())
        item.insert(QStringLiteral("group_id"), gid);
    return item;
}
} // namespace

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif

    QCoreApplication app(argc, argv);
    mychat::log::init();

    // 本地库放 exe 旁的隔离目录（%TEMP% 在受限环境里不可写，症状像"落库坏了"）
    const QString data_dir =
        QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("test-data/mychat-push-test"));
    QDir(data_dir).removeRecursively();
    qputenv("MYCHAT_DATA_DIR", data_dir.toUtf8());

    FakeServer server;
    if (!server.listen())
    {
        writeLine(stderr, QStringLiteral("[FAIL] 起不了本地假服务器"));
        return 5;
    }
    writeLine(stdout, QStringLiteral("[setup] 假 ChatServer 已就绪，端口 %1").arg(server.port()));

    // 控制器必须先建：登录成功时它的槽要先跑（会主动发一次 1023）
    ChatController chat;
    auto& socket = mychat::ChatSocket::instance();

    QVariantMap friend_item;
    friend_item.insert(QStringLiteral("uid"), QString::fromLatin1(kPeerUid));
    friend_item.insert(QStringLiteral("name"), QStringLiteral("peer"));
    friend_item.insert(QStringLiteral("displayName"), QStringLiteral("peer"));
    chat.syncFriends({friend_item});

    int fail_count = 0;
    const auto fail = [&](const QString& why)
        {
            writeLine(stderr, QStringLiteral("[FAIL] %1").arg(why));
            ++fail_count;
        };

    const auto check = [&](const bool ok, const QString& what)
        {
            if (ok)
            {
                writeLine(stdout, QStringLiteral("[OK] %1").arg(what));
                return true;
            }
            fail(what);
            return false;
        };

    QTimer overall;
    overall.setSingleShot(true);
    QObject::connect(&overall, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 整体超时"));
                             app.exit(4);
                         });

    // ── 观察点 ─────────────────────────────────────────────────────
    int pull_count = 0;
    int ack_count = 0;
    QStringList last_ack_ids;
    QStringList ack_batches;
    int push_ack_after = -1; // 第一次 1047 之后的 ack_count 快照
    int text_req_count = 0;
    bool text_req_group = false;
    QStringList system_messages;

    const auto conversations = [&chat]() { return chat.conversations(); };
    const auto conversationOf = [&](const QString& uid) -> QVariantMap
        {
            const QVariantList list = conversations();
            for (const QVariant& entry: list)
            {
                const QVariantMap item = entry.toMap();
                if (item.value(QStringLiteral("uid")).toString() == uid)
                    return item;
            }
            return {};
        };
    const auto messageTexts = [&chat]()
        {
            QStringList texts;
            for (const QVariant& entry: chat.messages())
                texts.append(entry.toMap().value(QStringLiteral("text")).toString());
            return texts;
        };
    const auto conversationKeys = [&]()
        {
            QStringList keys;
            for (const QVariant& entry: conversations())
                keys.append(entry.toMap().value(QStringLiteral("uid")).toString());
            return keys;
        };

    QObject::connect(&chat, &ChatController::groupSystemMessage,
                     &app, [&](const QString& gid, const QString& body)
                         {
                             system_messages.append(gid + QLatin1Char(':') + body);
                         });

    const auto finish = [&]()
        {
            check(ack_count == 2,
                  QStringLiteral("全程只确认了 2 批 1025（1047 推送一条都没确认），实际 %1")
                  .arg(ack_count));
            check(pull_count == 3,
                  QStringLiteral("只发了 3 次 1023（登录补拉 + 两次手动拉取），实际 %1")
                  .arg(pull_count));
            check(ack_batches.size() == 2
                      && ack_batches.at(0) == QStringLiteral("1734567891000-0,1734567891001-0")
                      && ack_batches.at(1) == QStringLiteral("1734567891100-0,1734567891101-0"),
                  QStringLiteral("1025 回传的是 1024 里的 Stream 编号（新格式取 time、旧格式取 id）"));
            check(system_messages == QStringList{QStringLiteral("7:邀请了 2 个好友")},
                  QStringLiteral("群系统消息抛给了页面（gid:正文）"));

            // 落盘：控制器开着同一个库，这里另开一个连接读
            {
                ChatStore store;
                const bool opened = store.openAt(ChatStore::databasePathFor(666672));
                check(opened, QStringLiteral("登录时已按 uid 建好本地库"));
                if (opened)
                {
                    const QList<ChatStore::Message> peer_rows =
                        store.recentMessages(QString::fromLatin1(kPeerUid), 100);
                    check(peer_rows.size() == 3,
                          QStringLiteral("单聊会话落盘 3 条（1 推送 + 2 拉取），实际 %1")
                          .arg(peer_rows.size()));

                    const QList<ChatStore::Message> group_rows =
                        store.recentMessages(QStringLiteral("g7"), 100);
                    check(group_rows.size() == 2,
                          QStringLiteral("群 g7 落盘 2 条（群消息 + 系统消息），实际 %1")
                          .arg(group_rows.size()));

                    bool system_flag = false;
                    bool system_has_sender = true;
                    for (const ChatStore::Message& row: group_rows)
                    {
                        if (row.body == QStringLiteral("邀请了 2 个好友"))
                        {
                            system_flag = true;
                            system_has_sender = !row.fromUid.isEmpty();
                        }
                    }
                    check(system_flag, QStringLiteral("系统消息正文落盘"));
                    check(system_has_sender,
                          QStringLiteral("系统消息的 from_uid 落成 \"1\"（界面据此居中渲染）"));
                }
            }

            QDir(data_dir).removeRecursively();

            writeLine(stdout, fail_count == 0
                          ? QStringLiteral("\n[PASS] 直推链路全部通过")
                          : QStringLiteral("\n[FAIL] 共 %1 项失败").arg(fail_count));
            app.exit(fail_count == 0 ? 0 : 3);
        };

    // ── 假服务器的行为脚本 ─────────────────────────────────────────
    server.onFrame = [&](const std::uint16_t id, const QByteArray& body)
        {
            if (id == kLoginReq)
            {
                server.replyJson(kLoginRsp,
                                 QJsonObject{
                                     {QStringLiteral("error"), 0},
                                     {QStringLiteral("uid"), QString::fromLatin1(kSelfUid)},
                                     {QStringLiteral("token"), QStringLiteral("fake-token")},
                                     {QStringLiteral("name"), QStringLiteral("tester")}
                                 });
                return;
            }

            if (id == kSyncChatMessageReq)
            {
                ++pull_count;
                const QJsonObject request = QJsonDocument::fromJson(body).object();
                check(request.value(QStringLiteral("count")).isString(),
                      QStringLiteral("1023 的 count 是字符串"));

                if (pull_count == 1)
                {
                    // 登录后的主动拉取：没有离线消息
                    server.replyJson(kSyncChatMessageRsp,
                                     QJsonObject{{QStringLiteral("error"), 0},
                                                 {QStringLiteral("count"), QStringLiteral("0")}});
                    return;
                }

                if (pull_count == 2)
                {
                    // 新格式：id 改名 time、group_id 恒出现（单聊 "0"）
                    QJsonObject rsp;
                    rsp.insert(QStringLiteral("error"), 0);
                    rsp.insert(QStringLiteral("count"), QStringLiteral("2"));
                    QJsonArray msgs;
                    msgs.append(makeMsgNew(kPeerUid, QStringLiteral("0"),
                                           QStringLiteral("离线单聊"), QStringLiteral("1734567891000-0")));
                    msgs.append(makeMsgNew(kStrangerUid, QStringLiteral("9"),
                                           QStringLiteral("离线群消息"), QStringLiteral("1734567891001-0")));
                    rsp.insert(QStringLiteral("msgs"), msgs);
                    server.replyJson(kSyncChatMessageRsp, rsp);
                    return;
                }

                if (pull_count == 3)
                {
                    // 旧格式：只有 id + 布尔 group（群消息还带 group_id）
                    QJsonObject rsp;
                    rsp.insert(QStringLiteral("error"), 0);
                    rsp.insert(QStringLiteral("count"), QStringLiteral("2"));
                    QJsonArray msgs;
                    msgs.append(makeMsgLegacy(kPeerUid, false, QString(),
                                              QStringLiteral("旧格式单聊"), QStringLiteral("1734567891100-0")));
                    msgs.append(makeMsgLegacy(kStrangerUid, true, QStringLiteral("9"),
                                              QStringLiteral("旧格式群消息"), QStringLiteral("1734567891101-0")));
                    rsp.insert(QStringLiteral("msgs"), msgs);
                    server.replyJson(kSyncChatMessageRsp, rsp);
                    return;
                }

                server.replyJson(kSyncChatMessageRsp,
                                 QJsonObject{{QStringLiteral("error"), 0},
                                             {QStringLiteral("count"), QStringLiteral("0")}});
                return;
            }

            if (id == kAckChatMessageReq)
            {
                ++ack_count;
                const QJsonObject request = QJsonDocument::fromJson(body).object();
                const QJsonArray ids = request.value(QStringLiteral("ids")).toArray();
                QStringList joined;
                for (const QJsonValue& value: ids)
                    joined.append(value.toString());
                ack_batches.append(joined.join(QLatin1Char(',')));
                last_ack_ids = joined;

                server.replyJson(kAckChatMessageRsp, QJsonObject{{QStringLiteral("error"), 0}});
                return;
            }

            if (id == kTextChatMsgReq)
            {
                ++text_req_count;
                const QJsonObject request = QJsonDocument::fromJson(body).object();
                text_req_group = request.value(QStringLiteral("group")).toBool(false);

                // 群消息发不出去：2026-10-24 起是 1023（不再是 1022）
                QJsonObject rsp;
                rsp.insert(QStringLiteral("error"), 1023);
                rsp.insert(QStringLiteral("msg_id"), request.value(QStringLiteral("msg_id")));
                server.replyJson(kTextChatMsgRsp, rsp);
                return;
            }
        };

    // ── 步骤驱动：每一步先断言上一步的结果，再触发下一步 ───────────
    int stage = 0;
    QTimer driver;
    driver.setInterval(kTickMs);
    QObject::connect(&driver, &QTimer::timeout, &app, [&]()
                         {
                             const int step = stage++;
                             switch (step)
                             {
                             case 0:
                                 check(pull_count == 1,
                                       QStringLiteral("登录成功后主动拉了一次（1023）"));
                                 server.replyJson(kChatMessagePush,
                                                  makePush(kPeerUid, QStringLiteral("0"),
                                                           QStringLiteral("直推消息"), kSingleTime1));
                                 break;

                             case 1:
                                 check(messageTexts() == QStringList{QStringLiteral("直推消息")},
                                       QStringLiteral("1047 单聊消息直接上屏（不经过拉取）"));
                                 check(conversationOf(QString::fromLatin1(kPeerUid))
                                           .value(QStringLiteral("lastText")).toString()
                                           == QStringLiteral("直推消息"),
                                       QStringLiteral("会话摘要更新"));
                                 push_ack_after = ack_count;
                                 check(ack_count == 0,
                                       QStringLiteral("1047 不触发 1025 确认"));
                                 // 同一条再推一次（同一个 time）
                                 server.replyJson(kChatMessagePush,
                                                  makePush(kPeerUid, QStringLiteral("0"),
                                                           QStringLiteral("直推消息"), kSingleTime1));
                                 break;

                             case 2:
                                 check(chat.messages().size() == 1,
                                       QStringLiteral("重复的 1047（同 time）只显示一次"));
                                 // group_id "0" 必须被当成单聊，不能建出 g0
                                 check(!conversationKeys().contains(QStringLiteral("g0")),
                                       QStringLiteral("group_id = \"0\" 没有建成群会话 g0"));
                                 server.replyJson(kChatMessagePush,
                                                  makePush(kStrangerUid, QStringLiteral("7"),
                                                           QStringLiteral("群里的消息"), kGroupTime));
                                 break;

                             case 3:
                                 {
                                     const QVariantMap group =
                                         conversationOf(QStringLiteral("g7"));
                                     check(!group.isEmpty(),
                                           QStringLiteral("1047 群消息自动建了会话 g7"));
                                     check(group.value(QStringLiteral("kind")).toString()
                                               == QStringLiteral("group"),
                                           QStringLiteral("g7 的 kind = group"));
                                     check(group.value(QStringLiteral("unread")).toInt() == 1,
                                           QStringLiteral("非当前会话的群消息记 1 条未读"));
                                     check(chat.messages().size() == 1,
                                           QStringLiteral("群消息没有混进当前会话"));
                                 }
                                 // 自己的群消息回显：服务端会扇出给发送者本人
                                 server.replyJson(kChatMessagePush,
                                                  makePush(kSelfUid, QStringLiteral("7"),
                                                           QStringLiteral("我自己发的"), kSelfEchoTime));
                                 break;

                             case 4:
                                 check(conversationOf(QStringLiteral("g7"))
                                           .value(QStringLiteral("lastText")).toString()
                                           == QStringLiteral("群里的消息"),
                                       QStringLiteral("自己的群消息回显被丢弃（摘要没被改）"));
                                 check(conversationOf(QStringLiteral("g7"))
                                           .value(QStringLiteral("unread")).toInt() == 1,
                                       QStringLiteral("自己的群消息回显不算未读"));
                                 // 群操作系统消息（from_uid = 1）
                                 server.replyJson(kChatMessagePush,
                                                  makePush("1", QStringLiteral("7"),
                                                           QStringLiteral("邀请了 2 个好友"), kSystemTime));
                                 break;

                             case 5:
                                 check(system_messages.size() == 1,
                                       QStringLiteral("群系统消息触发了 groupSystemMessage"));
                                 check(conversationOf(QStringLiteral("g7"))
                                           .value(QStringLiteral("unread")).toInt() == 2,
                                       QStringLiteral("系统消息也计未读（与拉取路径一致）"));
                                 // 陌生人单聊
                                 server.replyJson(kChatMessagePush,
                                                  makePush(kStrangerUid, QStringLiteral("0"),
                                                           QStringLiteral("陌生人直推"), kStrangerTime));
                                 break;

                             case 6:
                                 check(!conversationOf(QString::fromLatin1(kStrangerUid)).isEmpty(),
                                       QStringLiteral("陌生人直推也自动建会话"));
                                 check(conversationOf(QString::fromLatin1(kStrangerUid))
                                           .value(QStringLiteral("unread")).toInt() == 1,
                                       QStringLiteral("陌生人会话记 1 条未读"));
                                 check(ack_count == 0,
                                       QStringLiteral("到这一步仍然没有任何 1025（全程没确认推送）"));
                                 // 拉取新格式
                                 check(socket.syncChatMessages(2),
                                       QStringLiteral("发起一次 1023（新格式回包）"));
                                 break;

                             case 7:
                                 check(ack_count == 1,
                                       QStringLiteral("1024 新格式到达后确认了一批"));
                                 check(last_ack_ids == QStringList{QStringLiteral("1734567891000-0"),
                                                                   QStringLiteral("1734567891001-0")},
                                       QStringLiteral("确认用的是 time 里的 Stream 编号"));
                                 check(conversationOf(QStringLiteral("g9")).value(QStringLiteral("kind"))
                                           .toString() == QStringLiteral("group"),
                                       QStringLiteral("新格式 group_id = \"9\" 归到群会话 g9"));
                                 check(conversationOf(QString::fromLatin1(kPeerUid))
                                           .value(QStringLiteral("lastText")).toString()
                                           == QStringLiteral("离线单聊"),
                                       QStringLiteral("新格式单聊（group_id = \"0\"）落进单聊会话"));
                                 check(socket.syncChatMessages(2),
                                       QStringLiteral("再发起一次 1023（旧格式回包）"));
                                 break;

                             case 8:
                                 check(ack_count == 2,
                                       QStringLiteral("旧格式回包也确认了"));
                                 check(last_ack_ids == QStringList{QStringLiteral("1734567891100-0"),
                                                                   QStringLiteral("1734567891101-0")},
                                       QStringLiteral("旧格式确认用的是 id 字段"));
                                 check(conversationOf(QStringLiteral("g9"))
                                           .value(QStringLiteral("lastText")).toString()
                                           == QStringLiteral("旧格式群消息"),
                                       QStringLiteral("旧格式 group=true + id 仍能解析并归属"));
                                 {
                                     const QVariantMap peer_conversation =
                                         conversationOf(QString::fromLatin1(kPeerUid));
                                     check(peer_conversation.value(QStringLiteral("lastText")).toString()
                                               == QStringLiteral("旧格式单聊"),
                                           QStringLiteral("旧格式 group=false 的单聊照旧"));
                                 }

                                 // 换到群会话，验"不在群里"的错误码文案
                                 chat.openConversation(QStringLiteral("g9"), QStringLiteral("群 9"));
                                 check(chat.sendText(QStringLiteral("你好")),
                                       QStringLiteral("群会话里发一条消息（1013）"));
                                 break;

                             case 9:
                                 check(text_req_count == 1, QStringLiteral("群消息发了 1013"));
                                 check(text_req_group,
                                       QStringLiteral("群会话发消息带 group = true（JSON 布尔）"));
                                 check(chat.tip().contains(QStringLiteral("不在这个群里")),
                                       QStringLiteral("1023 也提示「你已不在这个群里」，实际：%1")
                                       .arg(chat.tip()));
                                 check(chat.tipIsError(), QStringLiteral("该提示按错误样式显示"));
                                 driver.stop();
                                 finish();
                                 break;

                             default:
                                 break;
                             }
                         });

    QObject::connect(&socket, &mychat::ChatSocket::connected, &app, [&]()
                         {
                             writeLine(stdout, QStringLiteral("[OK] 已连接假 ChatServer，开始登录"));
                             socket.login(666672, QStringLiteral("fake-token"));
                         });

    QObject::connect(&socket, &mychat::ChatSocket::loginSucceeded, &app,
                     [&](std::uint64_t, const QString&, const QString&)
                         {
                             // 控制器的槽先跑（它已经发出 1023）；登录会清会话表，
                             // 所以"当前会话"要在登录之后重新指定。
                             chat.openConversation(QString::fromLatin1(kPeerUid), QStringLiteral("peer"));
                             driver.start();
                         });

    overall.start(kOverallTimeoutMs);
    QTimer::singleShot(0, &app, [&]()
                           {
                               socket.connectToServer(QStringLiteral("127.0.0.1"), server.port());
                           });

    const int result = app.exec();
    writeLine(stdout, QStringLiteral("[exit] %1").arg(result));
    return result;
}
