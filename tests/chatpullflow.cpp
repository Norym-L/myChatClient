// 消息拉取链路自检（1022 推送 → 1023 拉取 → 1024 落库 → 1025 确认）
// —— 自带假 ChatServer，不需要真实服务端。
//
// 为什么要自带假服务器：这条链路的正确性全在"什么时候拉、拉多少、拉完确认什么"，
// 真实服务端只回数据、不回过程，测不出策略问题：
//
//   1. 登录后**主动拉一次**（离线消息不会补推 1022，不主动拉就永远看不到）；
//   2. `count` 必须是**字符串**，且 ≤10（服务端 >10 直接回 1020）；
//   3. 一条 1022 不会立刻拉 —— 等 2s 合并成一批；攒够 10 条以上才立刻拉；
//   4. 1024 的消息要按 `from_uid` 落进**对应**会话；不在当前会话的算未读；
//   5. **PEL 重投（at-least-once）**：同 id 的消息被重复下发时不许出现重复气泡，
//      但必须照样 ACK，否则服务端会一直重投；
//   6. 回来的条数达到请求的 count 说明还有积压 → 客户端应自己接着拉下一批。
//
// 用法（无需服务端、无需网络）：
//   qtClientChatPullTest
//
// 退出码：0 全部通过；3 某一步断言失败；4 整体超时；5 起不了本地监听。

#include <QCoreApplication>
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
#include "logger.h"

namespace
{
constexpr int kStepTimeoutMs = 8000;     // 拉取抖动 2s + 回包，留足余量
constexpr int kOverallTimeoutMs = 40000;

constexpr std::uint16_t kLoginReq = 1001;
constexpr std::uint16_t kLoginRsp = 1002;
constexpr std::uint16_t kNotifyReceiveMessage = 1022;
constexpr std::uint16_t kSyncChatMessageReq = 1023;
constexpr std::uint16_t kSyncChatMessageRsp = 1024;
constexpr std::uint16_t kAckChatMessageReq = 1025;
constexpr std::uint16_t kAckChatMessageRsp = 1026;

const char* kSelfUid = "666672";
/// 当前会话的对方（在 syncFriends 里建过会话）
const char* kPeerUid = "666674";
/// 没建过会话的陌生人：验证"收到陌生消息自动建会话 + 记未读"
const char* kStrangerUid = "666675";

void writeLine(FILE* stream, const QString& text)
{
    const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
    std::fflush(stream);
}

/// 假 ChatServer：只实现本测试用到的五帧
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

/// 拼一条 1024 用的消息项
QJsonObject makeMessage(const char* fromUid, const QString& body, const QString& id)
{
    QJsonObject item;
    item.insert(QStringLiteral("type"), 1); // 文档：1024 里的 type 是 number
    item.insert(QStringLiteral("from_uid"), QString::fromLatin1(fromUid));
    item.insert(QStringLiteral("body"), body);
    item.insert(QStringLiteral("id"), id);
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

    FakeServer server;
    if (!server.listen())
    {
        writeLine(stderr, QStringLiteral("[FAIL] 起不了本地假服务器"));
        return 5;
    }
    writeLine(stdout, QStringLiteral("[setup] 假 ChatServer 已就绪，端口 %1").arg(server.port()));

    // ⚠ 控制器必须先建：它的槽要抢在测试自己的 lambda 之前，
    //   才能保证"登录后主动拉取"由它先发出去（也正是要验的行为）
    ChatController chat;
    auto& socket = mychat::ChatSocket::instance();

    // 当前会话 = 好友 666674；666675 故意不建会话
    QVariantMap friend_item;
    friend_item.insert(QStringLiteral("uid"), QString::fromLatin1(kPeerUid));
    friend_item.insert(QStringLiteral("name"), QStringLiteral("peer"));
    friend_item.insert(QStringLiteral("displayName"), QStringLiteral("peer"));
    chat.syncFriends({friend_item});
    chat.openConversation(QString::fromLatin1(kPeerUid), QStringLiteral("peer"));

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

    QTimer step_timer;
    step_timer.setSingleShot(true);
    QObject::connect(&step_timer, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 这一步 8s 内没有结果"));
                             app.exit(3);
                         });

    QTimer overall;
    overall.setSingleShot(true);
    QObject::connect(&overall, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 整体超时"));
                             app.exit(4);
                         });

    // ── 测试状态 ───────────────────────────────────────────────────
    int pull_count = 0; // 收到过几次 1023
    int ack_count = 0;  // 收到过几次 1025
    int last_pull_count = -1;
    QElapsedTimer push_clock; // 1022 推出去的时刻（量 2s 合并窗口）
    bool burst_mode = false;  // true = 下一批 1022 是"11 连推"，要立刻拉

    /// 第一条消息的 id：第二批会用它冒充"PEL 重投"，验证客户端去重
    const QString first_id = QStringLiteral("1734567890123-0");

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

    /// 最终判据：所有断言跑完再退出，便于一次看到全部问题
    const auto finish = [&]()
        {
            check(chat.messages().size() == 11, QStringLiteral("当前会话共 11 条消息（1 + 10，重投的那条没重复）"));
            check(ack_count == 3, QStringLiteral("确认共 3 批（2 条 / 1 条重投 / 10 条）"));

            writeLine(stdout, fail_count == 0
                          ? QStringLiteral("\n[PASS] 拉取链路全部通过")
                          : QStringLiteral("\n[FAIL] 共 %1 项失败").arg(fail_count));
            app.exit(fail_count == 0 ? 0 : 3);
        };

    // ── 假服务器的行为脚本（按第几次 1023 分支）────────────────────
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

                // count 必须是字符串（服务端 ParseNumber(root["count"].asString())）
                const QJsonObject request = QJsonDocument::fromJson(body).object();
                const QJsonValue count_value = request.value(QStringLiteral("count"));
                check(count_value.isString(),
                      QStringLiteral("1023 的 count 是字符串（第 %1 次）").arg(pull_count));
                last_pull_count = count_value.toString().toInt();

                if (pull_count == 1)
                {
                    // 登录后的主动拉取：应该一次拉满 10 条
                    check(last_pull_count == 10,
                          QStringLiteral("登录后主动拉取，count = 10（拉满）"));

                    QJsonObject rsp;
                    rsp.insert(QStringLiteral("error"), 0);
                    // ⚠ 2026-10-18 晚起 count 是**字符串**（服务端 to_string）
                    rsp.insert(QStringLiteral("count"), QStringLiteral("2"));
                    QJsonArray msgs;
                    msgs.append(makeMessage(kPeerUid, QStringLiteral("在吗？"), first_id));
                    msgs.append(makeMessage(kStrangerUid, QStringLiteral("陌生人消息"),
                                            QStringLiteral("1734567890124-0")));
                    rsp.insert(QStringLiteral("msgs"), msgs);
                    server.replyJson(kSyncChatMessageRsp, rsp);
                    return;
                }

                if (pull_count == 2)
                {
                    // 推送一条 1022 之后触发的那次：等满 2s 才拉，且只拉 1 条
                    check(last_pull_count == 1,
                          QStringLiteral("1 条 1022 → count = 1（按提示条数拉）"));
                    const qint64 elapsed = push_clock.elapsed();
                    check(elapsed >= 1500,
                          QStringLiteral("合并窗口生效：等了 %1ms 才拉（≥1500ms）").arg(elapsed));

                    // PEL 重投：把第一条**原样再发一次**（同 id），客户端必须去重
                    QJsonObject rsp;
                    rsp.insert(QStringLiteral("error"), 0);
                    rsp.insert(QStringLiteral("count"), QStringLiteral("1"));
                    QJsonArray msgs;
                    msgs.append(makeMessage(kPeerUid, QStringLiteral("在吗？"), first_id));
                    rsp.insert(QStringLiteral("msgs"), msgs);
                    server.replyJson(kSyncChatMessageRsp, rsp);
                    return;
                }

                if (pull_count == 3)
                {
                    // 上一批拉回了 1 条（== 请求的 count），客户端应自己续拉下一批；
                    // 此时 PEL 已空，回空包让它停下来。
                    check(last_pull_count == 10,
                          QStringLiteral("拉满请求条数后自动续拉（count 仍为 10）"));

                    QJsonObject rsp;
                    rsp.insert(QStringLiteral("error"), 0);
                    rsp.insert(QStringLiteral("count"), QStringLiteral("0"));
                    server.replyJson(kSyncChatMessageRsp, rsp);

                    // 下一阶段：11 条 1022 连推 → 超过阈值应立刻拉
                    QTimer::singleShot(200, &app, [&]()
                                           {
                                               burst_mode = true;
                                               push_clock.start();
                                               for (int i = 0; i < 11; ++i)
                                                   server.reply(kNotifyReceiveMessage,
                                                                R"({"error":0})");
                                           });
                    return;
                }

                if (pull_count == 4)
                {
                    // 11 条 1022 连推 → 超过阈值，应该立刻拉，而不是等 2s
                    check(burst_mode, QStringLiteral("第 4 次拉取来自 11 连推（立刻拉）"));
                    check(push_clock.elapsed() <= 1500,
                          QStringLiteral("11 连推后立刻拉（%1ms，没等 2s 窗口）")
                          .arg(push_clock.elapsed()));
                    check(last_pull_count == 10,
                          QStringLiteral("11 条 1022 → count 夹到 10（服务端上限）"));

                    QJsonObject rsp;
                    rsp.insert(QStringLiteral("error"), 0);
                    rsp.insert(QStringLiteral("count"), QStringLiteral("10"));
                    QJsonArray msgs;
                    for (int i = 0; i < 10; ++i)
                    {
                        msgs.append(makeMessage(kPeerUid,
                                                QStringLiteral("积压消息 %1").arg(i + 1),
                                                QStringLiteral("17345678902%1-0").arg(i)));
                    }
                    rsp.insert(QStringLiteral("msgs"), msgs);
                    server.replyJson(kSyncChatMessageRsp, rsp);
                    return;
                }

                if (pull_count == 5)
                {
                    // 上一批拉满 10 条 → 客户端应自己接着拉下一批
                    check(last_pull_count == 10,
                          QStringLiteral("拉满 10 条后自动续拉下一批（count 仍为 10）"));
                    QJsonObject rsp;
                    rsp.insert(QStringLiteral("error"), 0);
                    rsp.insert(QStringLiteral("count"), QStringLiteral("0"));
                    server.replyJson(kSyncChatMessageRsp, rsp);

                    // 给客户端一拍把 state 处理完，再统一判据
                    QTimer::singleShot(400, &app, finish);
                    return;
                }

                // 不该有第 6 次
                fail(QStringLiteral("意外的第 %1 次拉取（PEL 抽干之后不该再拉）").arg(pull_count));
                return;
            }

            if (id == kAckChatMessageReq)
            {
                ++ack_count;

                const QJsonObject request = QJsonDocument::fromJson(body).object();
                const QJsonValue ids_value = request.value(QStringLiteral("ids"));
                check(ids_value.isArray(), QStringLiteral("1025 的 ids 是数组"));

                const QJsonArray ids = ids_value.toArray();
                if (ack_count == 1)
                {
                    check(ids.size() == 2, QStringLiteral("第一批确认 2 个 id"));
                    check(ids.first().isString(), QStringLiteral("ids[] 元素是字符串"));
                    check(ids.first().toString() == first_id,
                          QStringLiteral("确认的 id 与 1024 下发的一致"));
                }
                else if (ack_count == 2)
                {
                    // PEL 重投的那条被去重了，但**必须照样确认**，否则会一直被重投
                    check(ids.size() == 1 && ids.first().toString() == first_id,
                          QStringLiteral("重投的旧消息虽被去重，仍照常确认（1 个 id）"));
                }
                else if (ack_count == 3)
                {
                    check(ids.size() == 10, QStringLiteral("第三批确认 10 个 id（服务端上限）"));
                }

                server.replyJson(kAckChatMessageRsp, QJsonObject{{QStringLiteral("error"), 0}});
                return;
            }
        };

    QObject::connect(&socket, &mychat::ChatSocket::connected, &app, [&]()
                         {
                             writeLine(stdout, QStringLiteral("[OK] 已连接假 ChatServer，开始登录"));
                             step_timer.start(kStepTimeoutMs);
                             socket.login(666672, QStringLiteral("fake-token"));
                         });

    QObject::connect(&socket, &mychat::ChatSocket::loginSucceeded, &app,
                     [&](std::uint64_t, const QString&, const QString&)
                         {
                             // 控制器的槽先跑（它已经发出 1023），这里只推进后续步骤
                             step_timer.start(kStepTimeoutMs);
                         });

    // 每收到一批消息就核对控制器的落地结果（控制器先于本 lambda 处理完）
    QObject::connect(&socket, &mychat::ChatSocket::chatMessagesReceived, &app,
                     [&](const QList<mychat::protocol::ChatMessage>& messages, const int error)
                         {
                             step_timer.start(kStepTimeoutMs);

                             if (messages.size() == 2)
                             {
                                 check(error == 0, QStringLiteral("首批拉取 error = 0"));
                                 check(chat.messages().size() == 1,
                                       QStringLiteral("当前会话只落对方(666674)那 1 条"));
                                 check(conversations().size() == 2,
                                       QStringLiteral("陌生人消息自动建了 1 个会话"));

                                 const QVariantMap stranger =
                                     conversationOf(QString::fromLatin1(kStrangerUid));
                                 check(stranger.value(QStringLiteral("unread")).toInt() == 1,
                                       QStringLiteral("非当前会话的未读 +1"));
                                 check(stranger.value(QStringLiteral("lastText")).toString()
                                         == QStringLiteral("陌生人消息"),
                                       QStringLiteral("会话摘要更新为最新一条"));

                                 // 下一阶段：推一条 1022，验 2s 合并窗口
                                 push_clock.start();
                                 server.reply(kNotifyReceiveMessage,
                                              R"({"error":0})");
                                 return;
                             }

                             if (messages.size() == 1)
                             {
                                 // PEL 重投一条同 id 的旧消息 → 不该多出一个气泡
                                 check(chat.messages().size() == 1,
                                       QStringLiteral("PEL 重投的消息按 id 去重，没有重复气泡"));
                                 return;
                             }

                             if (messages.size() == 10)
                             {
                                 check(chat.messages().size() == 11,
                                       QStringLiteral("续拉的 10 条都落进当前会话"));
                                 check(chat.messages().last().toMap()
                                         .value(QStringLiteral("text")).toString()
                                         == QStringLiteral("积压消息 10"),
                                       QStringLiteral("消息顺序与 Stream 顺序一致"));
                                 return;
                             }
                         });

    // 统一超时兜底（每步已在信号里重置）
    step_timer.start(kStepTimeoutMs);

    overall.start(kOverallTimeoutMs);
    QTimer::singleShot(0, &app, [&]()
                           {
                               socket.connectToServer(QStringLiteral("127.0.0.1"), server.port());
                           });

    const int result = app.exec();
    writeLine(stdout, QStringLiteral("[exit] %1").arg(result));
    return result;
}
