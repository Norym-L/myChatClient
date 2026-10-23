// 群聊链路自检（1033–1042 + 群消息 1013/1024/1025）—— 自带假 ChatServer。
//
// 为什么不用真实服务端：这条链路上有一堆"只能靠抓包看"的约定，真实服务端
// 只会回 {"error":0}，不告诉你请求体长什么样，也不保证测试账号里有群：
//
//   1. 创建群 1033 的字段名是 `avator`（**服务端拼错的**），发 `avatar` 会被静默忽略；
//   2. 拉人 1035 的 `members` 必须是**数组**（不是字符串），否则服务端回 1020；
//   3. 群成员 1041 的字段名是 `gid`，和本组其它接口的 `group_id` 不一致；
//   4. 群消息 1013 的 `group` 必须是 **JSON 布尔**（服务端 asBool() 对字符串也返回真，
//      发 "false" 会被当成 true —— 这种错在真实服务端上测不出来）；
//   5. 群会话的键是 `g<gid>`（群号和用户 uid 都是数字，不加前缀会撞号），
//      而且 `syncFriends` **不能**把它当成"非好友空壳"删掉；
//   6. 服务端会把群消息也写进**发送者自己**的离线流 —— 拉回来必须丢弃回显，
//      但仍然要 ACK，否则 PEL 会一直重投；
//   7. 群操作通知没有独立类型：服务端只把 from_uid 置为保留的 "1"（api-group.md
//      §9.5）。客户端要把它标成 system、渲染成居中提示，并且不能拿 uid 1 去
//      解析发送者名字；解散后被本地移除的群也不能被这条系统消息"复活"。
//
// 用法（无需服务端、无需网络）：
//   qtClientGroupTest
//
// 退出码：0 全部通过；3 某一步断言失败；4 整体超时；5 起不了本地监听。

#include <QCoreApplication>
#include <QDir>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantMap>

#include <cstdint>
#include <cstdio>
#include <functional>

#include "ChatController.h"
#include "ChatSocket.h"
#include "GroupController.h"
#include "logger.h"

namespace
{
constexpr int kStepTimeoutMs = 7000;
constexpr int kOverallTimeoutMs = 45000;

constexpr std::uint16_t kLoginReq = 1001;
constexpr std::uint16_t kLoginRsp = 1002;
constexpr std::uint16_t kTextMsgReq = 1013;
constexpr std::uint16_t kTextMsgRsp = 1014;
constexpr std::uint16_t kSyncMsgReq = 1023;
constexpr std::uint16_t kSyncMsgRsp = 1024;
constexpr std::uint16_t kAckMsgReq = 1025;
constexpr std::uint16_t kAckMsgRsp = 1026;
constexpr std::uint16_t kGroupCreateReq = 1033;
constexpr std::uint16_t kGroupCreateRsp = 1034;
constexpr std::uint16_t kGroupAddReq = 1035;
constexpr std::uint16_t kGroupAddRsp = 1036;
constexpr std::uint16_t kGroupKickReq = 1037;
constexpr std::uint16_t kGroupKickRsp = 1038;
constexpr std::uint16_t kGroupDismissReq = 1039;
constexpr std::uint16_t kGroupDismissRsp = 1040;
constexpr std::uint16_t kGroupMembersReq = 1041;
constexpr std::uint16_t kGroupMembersRsp = 1042;

const char* kSelfUid = "666672";
const char* kGroupMateUid = "666674";
const char* kGid = "20001";
/// 群会话在客户端里的键（前缀约定见 ChatController::groupKey）
const char* kGroupKey = "g20001";

void writeLine(FILE* stream, const QString& text)
{
    const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
    std::fflush(stream);
}

int g_failures = 0;

void check(const bool condition, const QString& what)
{
    if (condition)
    {
        writeLine(stdout, QStringLiteral("  [OK] ") + what);
        return;
    }

    ++g_failures;
    writeLine(stdout, QStringLiteral("  [FAIL] ") + what);
}

QJsonObject parse(const QByteArray& body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    return document.isObject() ? document.object() : QJsonObject{};
}

/// 假 ChatServer：按请求型号回固定应答，请求体原样留档供断言
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

    [[nodiscard]] bool listen()
    {
        return m_server.listen(QHostAddress::LocalHost, 0);
    }

    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }

    std::function<void(std::uint16_t, const QJsonObject&)> onFrame;

    QList<QPair<std::uint16_t, QByteArray>> frames;

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

    /// 最近一帧指定型号的请求体
    [[nodiscard]] QJsonObject lastOf(const std::uint16_t id) const
    {
        for (auto it = frames.crbegin(); it != frames.crend(); ++it)
        {
            if (it->first == id)
                return parse(it->second);
        }
        return {};
    }

    [[nodiscard]] int countOf(const std::uint16_t id) const
    {
        int count = 0;
        for (const auto& frame: frames)
        {
            if (frame.first == id)
                ++count;
        }
        return count;
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

            frames.append({id, body});

            if (onFrame)
                onFrame(id, parse(body));
        }
    }

    QTcpServer m_server;
    QTcpSocket* m_peer = nullptr;
    QByteArray m_buffer;
};

/// 从 conversations 里找某个会话（按 uid / 会话键）
QVariantMap conversationOf(const QVariantList& list, const QString& uid)
{
    for (const QVariant& entry: list)
    {
        const QVariantMap item = entry.toMap();
        if (item.value(QStringLiteral("uid")).toString() == uid)
            return item;
    }
    return {};
}
} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qtClientGroupTest"));

    /*
     * 临时库放在 exe 旁边而不是 %TEMP%：受限环境里 %TEMP% 可能不可写，
     * 那会让 ChatStore::open 直接失败 —— 看起来像功能坏了，其实是环境不让写。
     * 每次跑之前清掉，保证"第一条消息 id = 1"，1014 的 c-1 断言才成立。
     */
    const QString data_path = QDir(QCoreApplication::applicationDirPath())
                                  .filePath(QStringLiteral("test-data/mychat-group-test"));
    QDir(data_path).removeRecursively();
    if (!QDir().mkpath(data_path))
    {
        writeLine(stderr, QStringLiteral("[FAIL] 建不了临时数据目录: %1").arg(data_path));
        return 3;
    }
    qputenv("MYCHAT_DATA_DIR", data_path.toUtf8());

    FakeServer server;
    if (!server.listen())
    {
        writeLine(stderr, QStringLiteral("[FAIL] 假服务器起不了监听"));
        return 5;
    }

    ChatController chat;
    GroupController group;

    auto& socket = mychat::ChatSocket::instance();

    // 每步的看门狗：任何一步卡住都算失败，不要让测试挂死
    QTimer step_timer;
    step_timer.setSingleShot(true);
    QObject::connect(&step_timer, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 某一步超时"));
                             app.exit(4);
                         });

    QTimer overall;
    overall.setSingleShot(true);
    QObject::connect(&overall, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 整体超时"));
                             app.exit(4);
                         });

    /*
     * 阶段闸门：memberListChanged 会因为「拉一次成员」「踢完重拉」触发多次，
     * messagesChanged 也会因为「本地回显」「拉回消息」多次触发 ——
     * 没有闸门的话同一条断言链路会跑第二遍（建两个群、发两条消息）。
     */
    bool member_stage_done = false;
    bool push_sent = false;
    bool peer_stage_done = false;
    bool system_stage_done = false;

    /// 系统消息信号捕获（onChatMessages 里 emit，QML 侧据此强刷成员列表）
    bool system_signal_seen = false;
    QString system_signal_gid;
    QString system_signal_body;

    /*
     * 请 1024 空列表：客户端登录后会主动拉一次（离线补投），
     * 不回应的话它会 2s 后重试，把后面的断言搅乱。
     */
    auto reply_empty_pull = [&server]()
    {
        server.reply(kSyncMsgRsp, R"({"error":0,"msgid":null})");
    };

    server.onFrame = [&](const std::uint16_t id, const QJsonObject& body)
    {
        Q_UNUSED(body)

        switch (id)
        {
        case kLoginReq:
            server.reply(kLoginRsp,
                         R"({"error":0,"uid":"666672","token":"fake","name":"自检","has_pending":"0"})");
            break;

        case kSyncMsgReq:
            reply_empty_pull();
            break;

        case kGroupCreateReq:
            // 模拟服务端：成功建群，返回 group_id
            server.reply(kGroupCreateRsp, R"({"error":0,"msgid":null,"group_id":"20001"})");
            break;

        case kGroupAddReq:
            server.reply(kGroupAddRsp, R"({"error":0,"msgid":null})");
            break;

        case kGroupMembersReq:
            /*
             * 1042 的真实形状：uid 是**字符串**、role 是**数字**、
             * join_at 是毫秒时间戳**字符串**、nick_g 为 NULL 时是空串。
             * 没有 mb_infos 字段表示"没有成员"（这里有两名成员）。
             */
            server.reply(kGroupMembersRsp, R"({"error":0,"msgid":null,"mb_infos":[)"
                                           R"({"nick_g":"","join_at":"1734567890123","uid":"666672","role":2},)"
                                           R"({"nick_g":"群里的老四","join_at":"1734567899999","uid":"666674","role":0}]})");
            break;

        case kGroupKickReq:
            server.reply(kGroupKickRsp, R"({"error":0,"msgid":null})");
            break;

        case kGroupDismissReq:
            server.reply(kGroupDismissRsp, R"({"error":0,"msgid":null})");
            break;

        case kTextMsgReq:
            // 回显 msg_id（真实服务端就是这么回执的），测试按它认领气泡
            server.reply(kTextMsgRsp, R"({"error":0,"msgid":null,"msg_id":"c-1"})");
            break;

        case kAckMsgReq:
            server.reply(kAckMsgRsp, R"({"error":0,"msgid":null})");
            break;

        default:
            break;
        }
    };

    // ── 阶段 1：建群 → 拉人 → 拉成员 ─────────────────────────────
    QObject::connect(&socket, &mychat::ChatSocket::connected, &app, [&]()
                         {
                             writeLine(stdout, QStringLiteral("[step] 已连接假服务器，登录"));
                             step_timer.start(kStepTimeoutMs);
                             socket.login(666672, QStringLiteral("fake-token"));
                         });

    QObject::connect(&socket, &mychat::ChatSocket::groupCreated, &app,
                     [&](const bool ok, const int error, const QString& gid, const QString& name)
                         {
                             step_timer.start(kStepTimeoutMs);
                             check(ok && error == 0, QStringLiteral("1034 建群成功"));
                             check(gid == QLatin1String(kGid),
                                   QStringLiteral("建群回包里的 group_id = 20001"));
                             check(name == QStringLiteral("自检群"),
                                   QStringLiteral("建群回包带回请求侧群名"));

                             const QJsonObject body = server.lastOf(kGroupCreateReq);
                             check(body.value(QStringLiteral("name")).toString()
                                       == QStringLiteral("自检群"),
                                   QStringLiteral("1033 的 name 是「自检群」"));
                             check(body.value(QStringLiteral("avator")).isString(),
                                   QStringLiteral("1033 发的是 avator（服务端拼写），不是 avatar"));
                             check(!body.contains(QStringLiteral("avatar")),
                                   QStringLiteral("1033 不带 avatar 字段"));
                             check(body.value(QStringLiteral("announcement")).isString(),
                                   QStringLiteral("1033 的 announcement 是字符串"));
                         });

    QObject::connect(&socket, &mychat::ChatSocket::groupMembersAdded, &app,
                     [&](const bool ok, const int error, const QString& gid,
                         const QStringList& uids)
                         {
                             step_timer.start(kStepTimeoutMs);
                             check(ok && error == 0, QStringLiteral("1036 拉人成功"));
                             check(gid == QLatin1String(kGid), QStringLiteral("1036 带回 gid"));
                             check(uids == QStringList{QString::fromLatin1(kGroupMateUid)},
                                   QStringLiteral("1036 带回提交的成员列表"));

                             const QJsonObject body = server.lastOf(kGroupAddReq);
                             check(body.value(QStringLiteral("group_id")).toString()
                                       == QLatin1String(kGid),
                                   QStringLiteral("1035 的 group_id 是群号"));
                             const QJsonArray members = body.value(QStringLiteral("members")).toArray();
                             check(members.size() == 1
                                       && members.at(0).toString()
                                          == QLatin1String(kGroupMateUid),
                                   QStringLiteral("1035 的 members 是数组且元素为字符串 uid"));
                         });

    // ── 成员列表到位：1041 的字段名 + 1042 的解析 ─────────────────
    QObject::connect(&group, &GroupController::memberListChanged, &app, [&]()
                         {
                             if (member_stage_done || group.memberCount() == 0)
                                 return;
                             member_stage_done = true;

                             step_timer.start(kStepTimeoutMs);

                             const QJsonObject body = server.lastOf(kGroupMembersReq);
                             check(body.value(QStringLiteral("gid")).toString()
                                       == QLatin1String(kGid),
                                   QStringLiteral("1041 的字段名是 gid"));
                             check(!body.contains(QStringLiteral("group_id")),
                                   QStringLiteral("1041 不发 group_id（服务端只读 gid）"));

                             check(group.memberCount() == 2,
                                   QStringLiteral("1042 解析出 2 名成员"));
                             check(group.myRole() == 2,
                                   QStringLiteral("从成员列表认出自己是群主"));
                             check(group.isOwner() && group.canManage(),
                                   QStringLiteral("群主 canManage / isOwner"));

                             const QVariantMap mate =
                                 [&]()
                                 {
                                     for (const QVariant& entry: group.memberList())
                                     {
                                         const QVariantMap item = entry.toMap();
                                         if (item.value(QStringLiteral("uid")).toString()
                                             == QLatin1String(kGroupMateUid))
                                             return item;
                                     }
                                     return QVariantMap{};
                                 }();
                             check(mate.value(QStringLiteral("displayName")).toString()
                                       == QStringLiteral("群里的老四"),
                                   QStringLiteral("displayName 优先用群昵称 nick_g"));
                             check(mate.value(QStringLiteral("roleText")).toString()
                                       == QStringLiteral("成员"),
                                   QStringLiteral("role 0 → 成员"));
                             check(mate.value(QStringLiteral("canKick")).toBool(),
                                   QStringLiteral("群主可以踢普通成员"));

                             // ── 阶段 2：群会话登记 + 发群消息 ──
                             writeLine(stdout, QStringLiteral("[step] 登记群会话并发一条群消息"));
                             chat.registerGroup(QString::fromLatin1(kGid),
                                                QStringLiteral("自检群"), 2);

                             const QVariantMap conv =
                                 conversationOf(chat.conversations(),
                                                QString::fromLatin1(kGroupKey));
                             check(!conv.isEmpty(), QStringLiteral("群会话的键是 g<gid>"));
                             check(conv.value(QStringLiteral("kind")).toString()
                                       == QStringLiteral("group"),
                                   QStringLiteral("会话 kind = group"));
                             check(conv.value(QStringLiteral("gid")).toString()
                                       == QLatin1String(kGid),
                                   QStringLiteral("会话里带纯群号 gid"));
                             check(conv.value(QStringLiteral("displayName")).toString()
                                       == QStringLiteral("自检群"),
                                   QStringLiteral("会话显示名是群名"));

                             chat.openConversation(QString::fromLatin1(kGroupKey),
                                                   QStringLiteral("自检群"));
                             check(chat.activeIsGroup(),
                                   QStringLiteral("openConversation 后 activeIsGroup"));
                             check(chat.activeGroupId() == QLatin1String(kGid),
                                   QStringLiteral("activeGroupId 去掉了 g 前缀"));

                             check(chat.sendText(QStringLiteral("群消息自检")),
                                   QStringLiteral("sendText 返回 true"));
                         });

    // ── 阶段 3：1013 的 group 布尔 + 1014 回执 ────────────────────
    QObject::connect(&socket, &mychat::ChatSocket::textMessageResult, &app,
                     [&](const bool ok, const int error, const QString& msg_id,
                         const QString& to_uid, const QString& body)
                         {
                             Q_UNUSED(body)
                             step_timer.start(kStepTimeoutMs);

                             check(ok && error == 0, QStringLiteral("1014 发送成功"));

                             const QJsonObject request = server.lastOf(kTextMsgReq);
                             check(request.value(QStringLiteral("to_uid")).toString()
                                       == QLatin1String(kGid),
                                   QStringLiteral("群消息的 to_uid 是**群号**，不是会话键"));
                             check(request.value(QStringLiteral("group")).isBool(),
                                   QStringLiteral("1013 的 group 是 JSON 布尔"));
                             check(request.value(QStringLiteral("group")).toBool(),
                                   QStringLiteral("群消息的 group = true"));
                             check(msg_id == QStringLiteral("c-1"),
                                   QStringLiteral("1014 按 msg_id 认领到本地气泡 c-1"));
                             check(to_uid == QLatin1String(kGid),
                                   QStringLiteral("回执带回的 to_uid 是群号"));
                         });

    // 群操作系统消息信号：页面收到后会强刷成员列表（这里只验证 gid / 正文）
    QObject::connect(&chat, &ChatController::groupSystemMessage, &app,
                     [&](const QString& gid, const QString& body)
                         {
                             system_signal_seen = true;
                             system_signal_gid = gid;
                             system_signal_body = body;
                         });

    QObject::connect(&chat, &ChatController::messagesChanged, &app, [&]()
                         {
                             if (g_failures > 0)
                                 return;
                             if (chat.messages().isEmpty())
                                 return;

                             const QVariantMap last = chat.messages().last().toMap();

                             /*
                              * 系统消息（from_uid = "1"）：不能当普通气泡，
                              * 还要通过 groupSystemMessage 把群号 / 正文抛给页面。
                              */
                             if (!system_stage_done && last.value(QStringLiteral("system")).toBool())
                             {
                                 system_stage_done = true;
                                 step_timer.start(kStepTimeoutMs);

                                 check(last.value(QStringLiteral("text")).toString()
                                           == QStringLiteral("邀请了 2 个好友"),
                                       QStringLiteral("系统消息正文落进会话"));
                                 check(last.value(QStringLiteral("senderUid")).toString()
                                           == QStringLiteral("1"),
                                       QStringLiteral("系统消息的 senderUid = 1"));
                                 check(last.value(QStringLiteral("senderName")).toString().isEmpty(),
                                       QStringLiteral("系统消息不解析 uid 1 的发送者名字"));
                                 check(system_signal_seen
                                           && system_signal_gid == QLatin1String(kGid)
                                           && system_signal_body
                                               == QStringLiteral("邀请了 2 个好友"),
                                       QStringLiteral("groupSystemMessage 带出 gid / 正文"));

                                 // ── 阶段 6：踢人 → 解散 ──
                                 QTimer::singleShot(200, &app, [&]()
                                                        {
                                                            writeLine(stdout,
                                                                      QStringLiteral("[step] 踢人"));
                                                            step_timer.start(kStepTimeoutMs);
                                                            group.kickMember(
                                                                QString::fromLatin1(kGid),
                                                                QString::fromLatin1(kGroupMateUid));
                                                        });
                                 return;
                             }

                             if (peer_stage_done)
                                 return;

                             // 第一次：自己发的群消息变成 sent
                             if (!push_sent
                                 && last.value(QStringLiteral("text")).toString()
                                     == QStringLiteral("群消息自检")
                                 && last.value(QStringLiteral("state")).toString()
                                     == QStringLiteral("sent"))
                             {
                                 push_sent = true;

                                 // ── 阶段 4：服务端把群消息（含自己的回显）推回来 ──
                                 writeLine(stdout,
                                           QStringLiteral("[step] 推两条群消息（对方 + 自己的回显）"));
                                 step_timer.start(kStepTimeoutMs);
                                 server.reply(kSyncMsgRsp,
                                              R"({"error":0,"count":"2","msgs":[)"
                                              R"({"group":true,"group_id":"20001","type":1,)"
                                              R"("from_uid":"666674","body":"来自群友",)"
                                              R"("id":"1734567890123-0"},)"
                                              R"({"group":true,"group_id":"20001","type":1,)"
                                              R"("from_uid":"666672","body":"群消息自检",)"
                                              R"("id":"1734567890124-0"}]})");
                                 return;
                             }

                             // 第二次：只多出对方那一条（自己的回显被丢弃）
                             if (!peer_stage_done
                                 && last.value(QStringLiteral("text")).toString()
                                     == QStringLiteral("来自群友"))
                             {
                                 peer_stage_done = true;
                                 step_timer.start(kStepTimeoutMs);

                                 check(chat.messages().size() == 2,
                                       QStringLiteral("自己的群消息回显被丢弃，只多一条"));
                                 check(last.value(QStringLiteral("senderUid")).toString()
                                           == QLatin1String(kGroupMateUid),
                                       QStringLiteral("群消息记下了发送者 uid"));
                                 check(last.value(QStringLiteral("senderName")).toString()
                                           == QLatin1String(kGroupMateUid),
                                       QStringLiteral("好友列表还没同步时 senderName 退回 uid"
                                                      "（群昵称由 QML 从 GroupController 取）"));

                                 /*
                                  * 两条都要 ACK（自己的回显也要，否则 PEL 一直重投）。
                                  *
                                  * ⚠ ACK 是在 emit messagesChanged **之后**才发的
                                  *   （onChatMessages 的顺序：落库 → 发信号 → 确认），
                                  *   所以不能在信号里立刻断言，等这一拍事件循环走完。
                                  */
                                 QTimer::singleShot(300, &app, [&]()
                                                        {
                                                            bool acked_mine = false;
                                                            bool acked_peer = false;
                                                            for (const auto& frame: server.frames)
                                                            {
                                                                if (frame.first != kAckMsgReq)
                                                                    continue;
                                                                const QJsonObject ack = parse(frame.second);
                                                                for (const QJsonValue& value:
                                                                     ack.value(QStringLiteral("ids")).toArray())
                                                                {
                                                                    if (value.toString()
                                                                        == QStringLiteral("1734567890124-0"))
                                                                        acked_mine = true;
                                                                    if (value.toString()
                                                                        == QStringLiteral("1734567890123-0"))
                                                                        acked_peer = true;
                                                                }
                                                            }
                                                            check(acked_peer,
                                                                  QStringLiteral("对方的消息被 ACK"));
                                                            check(acked_mine,
                                                                  QStringLiteral("自己回显的消息也被 ACK"));

                                                            // ── 阶段 5：syncFriends 不能删群会话 ──
                                                            writeLine(stdout,
                                                                      QStringLiteral("[step] 好友同步（群会话必须保留）"));
                                                            step_timer.start(kStepTimeoutMs);
                                                            QVariantMap friend_item;
                                                            friend_item.insert(QStringLiteral("uid"),
                                                                               QString::fromLatin1(kGroupMateUid));
                                                            friend_item.insert(QStringLiteral("name"),
                                                                               QStringLiteral("三花"));
                                                            friend_item.insert(QStringLiteral("displayName"),
                                                                               QStringLiteral("三花"));
                                                            chat.syncFriends({friend_item});
                                                            check(!conversationOf(chat.conversations(),
                                                                                  QString::fromLatin1(kGroupKey))
                                                                       .isEmpty(),
                                                                  QStringLiteral("好友同步后群会话还在"));
                                                            check(!conversationOf(chat.conversations(),
                                                                                  QString::fromLatin1(kGroupMateUid))
                                                                       .isEmpty(),
                                                                  QStringLiteral("好友同步把 666674 补成会话"));

                                                            // ── 阶段 6：群操作系统消息（from_uid = 1）──
                                                            writeLine(stdout,
                                                                      QStringLiteral("[step] 推一条群操作系统消息"));
                                                            step_timer.start(kStepTimeoutMs);
                                                            server.reply(kSyncMsgRsp,
                                                                         R"({"error":0,"count":"1","msgs":[)"
                                                                         R"({"group":true,"group_id":"20001","type":1,)"
                                                                         R"("from_uid":"1","body":"邀请了 2 个好友",)"
                                                                         R"("id":"1734567890130-0"}]})");
                                                        });
                                 return;
                             }
                         });

    QObject::connect(&socket, &mychat::ChatSocket::groupMemberKicked, &app,
                     [&](const bool ok, const int error, const QString& gid, const QString& uid)
                         {
                             step_timer.start(kStepTimeoutMs);
                             check(ok && error == 0, QStringLiteral("1038 踢人成功"));

                             const QJsonObject body = server.lastOf(kGroupKickReq);
                             check(body.value(QStringLiteral("group_id")).toString()
                                       == QLatin1String(kGid),
                                   QStringLiteral("1037 的 group_id 是群号"));
                             check(body.value(QStringLiteral("uid")).toString()
                                       == uid,
                                   QStringLiteral("1037 的 uid 是被踢者"));
                             check(gid == QLatin1String(kGid),
                                   QStringLiteral("1038 带回 gid"));

                             // 踢完会自动重拉成员（1041）；busy 放行后再解散
                             QTimer::singleShot(300, &app, [&]()
                                                    {
                                                        writeLine(stdout,
                                                                  QStringLiteral("[step] 解散群聊"));
                                                        step_timer.start(kStepTimeoutMs);
                                                        group.dismissGroup(
                                                            QString::fromLatin1(kGid));
                                                    });
                         });

    QObject::connect(&socket, &mychat::ChatSocket::groupDismissed, &app,
                     [&](const bool ok, const int error, const QString& gid)
                         {
                             step_timer.start(kStepTimeoutMs);
                             check(ok && error == 0, QStringLiteral("1040 解散成功"));

                             const QJsonObject body = server.lastOf(kGroupDismissReq);
                             check(body.value(QStringLiteral("group_id")).toString()
                                       == QLatin1String(kGid),
                                   QStringLiteral("1039 的 group_id 是群号"));
                             check(gid == QLatin1String(kGid),
                                   QStringLiteral("1040 带回 gid"));

                             chat.removeGroupConversation(QString::fromLatin1(kGid));
                             check(conversationOf(chat.conversations(),
                                                  QString::fromLatin1(kGroupKey))
                                       .isEmpty(),
                                   QStringLiteral("解散后本地群会话被移除"));

                             /*
                              * 服务端解散时是「先写系统消息、再 DEL」（api-group.md §7），
                              * 这条 1024 可能在本机删掉会话之后才被拉到 ——
                              * 不能让它把 g20001 重新建出来。
                              */
                             server.reply(kSyncMsgRsp,
                                          R"({"error":0,"count":"1","msgs":[)"
                                          R"({"group":true,"group_id":"20001","type":1,)"
                                          R"("from_uid":"1","body":"该群聊已解散",)"
                                          R"("id":"1734567890140-0"}]})");

                             /*
                              * 同一时刻只允许一条群请求在飞 —— 但 setBusy(false) 是
                              * 回调的**最后一行**（订阅方会在 busyChanged 里接着做下一步），
                              * 所以在 groupDismissed 里直接读还是 true。
                              * 让事件循环走完这一拍再看。
                              */
                             QTimer::singleShot(300, &app, [&]()
                                                    {
                                                        check(conversationOf(chat.conversations(),
                                                                             QString::fromLatin1(kGroupKey))
                                                                  .isEmpty(),
                                                              QStringLiteral("本地移除的群不会被解散系统消息复活"));
                                                        check(!group.busy(),
                                                              QStringLiteral("解散结束后 busy 已复位"));

                                                        overall.stop();
                                                        step_timer.stop();

                                                        writeLine(stdout,
                                                                  g_failures == 0
                                                                      ? QStringLiteral("[PASS] 全部通过")
                                                                      : QStringLiteral("[FAIL] %1 项失败")
                                                                            .arg(g_failures));
                                                        app.exit(g_failures == 0 ? 0 : 3);
                                                    });
                         });

    // 登录成功后开建群流程；先给自己一点时间让控制器的登录槽跑完（它会发 1023）
    QObject::connect(&socket, &mychat::ChatSocket::loginSucceeded, &app,
                     [&](std::uint64_t, const QString&, const QString&)
                         {
                             QTimer::singleShot(150, &app, [&]()
                                                    {
                                                        writeLine(stdout,
                                                                  QStringLiteral("[step] 建群（1033 → 1035 → 1041）"));
                                                        step_timer.start(kStepTimeoutMs);
                                                        group.createGroup(
                                                            QStringLiteral("自检群"),
                                                            {QString::fromLatin1(kGroupMateUid)});
                                                    });
                         });

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
