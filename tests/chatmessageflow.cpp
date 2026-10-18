// 消息发送链路自检（1013 / 1014 / 1022）—— 自带假 ChatServer，不需要真实服务端。
//
// 为什么要自带假服务器：这条链路上有几个"只能靠约定、没法靠日志看出对错"的点，
// 拿真实服务端测不出来（它只回 {"error":0}，不告诉你请求体长什么样）：
//
//   1. 1013 请求体的四个字段（msg_id / to_uid / type / body）必须都是**字符串**
//      （api-chat.md §8.1）。服务端 `asString()` 对数字也兼容，所以"发错了也能通"
//      —— 只能在这里钉住。
//   2. 1014 会把 msg_id **原样回显** → 客户端按它精确认领（不依赖发送顺序）。
//      这里同时验证"上一条没回包之前，第二条绝不上线路"（队列层仍然串行）。
//   3. error=0 只是"写进了离线消息流"，error=1020 是参数非法，两者要走不同的状态。
//   4. 服务端一句都不回时，必须在回包超时窗口（5s）后报 RequestTimeout，
//      否则界面上会永远停在「发送中」。
//   5. 1022 推送只带 {"error":0} —— 客户端只能当作"有新消息"的信号。
//
// 用法（无需服务端、无需网络）：
//   qtClientChatMessageTest
//
// 退出码：0 全部通过；3 某一步断言失败；4 整体超时；5 起不了本地监听。

#include <QCoreApplication>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdint>
#include <cstdio>
#include <functional>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "ChatSocket.h"
#include "logger.h"

namespace
{
/// 每步容忍 7s（客户端回包超时窗口是 5s，留 2s 余量）
constexpr int kStepTimeoutMs = 7000;
constexpr int kOverallTimeoutMs = 45000;

/// protocol::ErrorCode::RequestTimeout —— 客户端本地超时码
constexpr int kTimeoutErrorCode = 5;
/// protocol::ErrorCode::RequestValueInvalid —— 服务端对非法 to_uid / type 的回包
constexpr int kInvalidValueErrorCode = 1020;

constexpr std::uint16_t kLoginReq = 1001;
constexpr std::uint16_t kLoginRsp = 1002;
constexpr std::uint16_t kTextMsgReq = 1013;
constexpr std::uint16_t kTextMsgRsp = 1014;
constexpr std::uint16_t kNotifyReceiveMessage = 1022;

const char* kSelfUid = "666672";
const char* kPeerUid = "666673";

// 每条消息的客户端标识（真实客户端里是 "c-<本地气泡 id>"，这里固定下来好断言）
const char* kMsg1 = "c-1";
const char* kMsg2 = "c-2";
const char* kMsg3 = "c-3";
const char* kMsg4 = "c-4";

void writeLine(FILE* stream, const QString& text)
{
    const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
    std::fflush(stream);
}

/**
 * 假 ChatServer：只处理本测试用到的四帧，其余照收不答。
 *
 * 帧格式与真实服务端一致：[message_id:2 大端][body_len:2 大端][body]。
 */
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

    /// 收到一帧时回调（已是拆好的单帧）
    std::function<void(std::uint16_t, const QByteArray&)> onFrame;

    /// 收到过的全部帧（按到达顺序），断言时用
    QList<QPair<std::uint16_t, QByteArray>> frames;

    /// true = 只收不答（模拟"服务端某个分支直接 return、一句都不回"）
    bool mute = false;

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
                onFrame(id, body);
        }
    }

    QTcpServer m_server;
    QTcpSocket* m_peer = nullptr;
    QByteArray m_buffer;
};

/**
 * 解析一帧 1013 的请求体，并校验四个字段的**类型与取值**。
 *
 * @return 空串 = 合法；否则返回一条人话错误描述
 */
QString checkTextMessageBody(const QByteArray& body, const QString& expect_msg_id,
                             const QString& expect_to_uid, const QString& expect_text)
{
    QJsonParseError parse_error{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return QStringLiteral("1013 请求体不是合法 JSON 对象: %1").arg(QString::fromUtf8(body));

    const QJsonObject object = document.object();

    // ⚠ 服务端按 asString() 读：**数字也能被兼容**，所以类型错了不会报错，
    //   只会变成"某天服务端收紧校验就全炸"。这里必须显式钉住"是字符串"。
    for (const QString& key: {QStringLiteral("msg_id"), QStringLiteral("to_uid"),
                              QStringLiteral("type"), QStringLiteral("body")})
    {
        if (!object.contains(key))
            return QStringLiteral("1013 请求体缺少字段 %1: %2")
                .arg(key, QString::fromUtf8(body));
        if (!object.value(key).isString())
            return QStringLiteral("1013 的 %1 必须是字符串（服务端按 asString 读）: %2")
                .arg(key, QString::fromUtf8(body));
    }

    if (object.value(QStringLiteral("msg_id")).toString() != expect_msg_id)
        return QStringLiteral("1013 的 msg_id 不对: 期望 %1，实际 %2")
            .arg(expect_msg_id, object.value(QStringLiteral("msg_id")).toString());

    if (object.value(QStringLiteral("to_uid")).toString() != expect_to_uid)
        return QStringLiteral("1013 的 to_uid 不对: 期望 %1，实际 %2")
            .arg(expect_to_uid, object.value(QStringLiteral("to_uid")).toString());

    if (object.value(QStringLiteral("type")).toString() != QStringLiteral("1"))
        return QStringLiteral("1013 的 type 应为 \"1\"（文本），实际 %1")
            .arg(object.value(QStringLiteral("type")).toString());

    if (object.value(QStringLiteral("body")).toString() != expect_text)
        return QStringLiteral("1013 的 body 不对: 期望「%1」，实际「%2」")
            .arg(expect_text, object.value(QStringLiteral("body")).toString());

    return {};
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

    auto& socket = mychat::ChatSocket::instance();

    // ── 测试状态 ───────────────────────────────────────────────────
    int fail_count = 0;
    int text_result_count = 0;   // 收到过几次 textMessageResult
    bool notify_received = false;

    const auto fail = [&](const QString& why)
        {
            writeLine(stderr, QStringLiteral("[FAIL] %1").arg(why));
            ++fail_count;
        };

    QTimer step_timer;
    step_timer.setSingleShot(true);
    QObject::connect(&step_timer, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 这一步 7s 内没有任何结果"));
                             app.exit(3);
                         });

    QTimer overall;
    overall.setSingleShot(true);
    QObject::connect(&overall, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 整体超时"));
                             app.exit(4);
                         });

    // 每条断言通过都打一行，失败继续跑（一次看到全部问题比只看到第一个有用）
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

    /*
     * 假服务器的行为脚本 —— 按收到的第几条 1013 来分支。
     * 用计数器而不是靠时序，读数更直观。
     */
    int text_frames = 0;

    server.onFrame = [&](const std::uint16_t id, const QByteArray& body)
        {
            if (id == kLoginReq)
            {
                const QString json = QString::fromUtf8(
                    R"({"error":0,"uid":"%1","token":"fake-token","name":"tester"})")
                    .arg(QString::fromLatin1(kSelfUid));
                server.reply(kLoginRsp, json.toUtf8());
                return;
            }

            if (id == kTextMsgReq)
            {
                if (server.mute)
                    return; // 只收不答：验证客户端自己会超时收尾

                ++text_frames;

                if (text_frames == 1)
                {
                    const QString err = checkTextMessageBody(
                        body, QString::fromLatin1(kMsg1), QString::fromLatin1(kPeerUid),
                        QStringLiteral("你好"));
                    if (!err.isEmpty())
                        fail(err);
                    else
                        writeLine(stdout, QStringLiteral("[OK] 1013 请求体字段与类型正确（含 msg_id）"));
                    // 回执必须原样回显 msg_id —— 这是客户端唯一的认领依据
                    server.reply(kTextMsgRsp,
                                 QStringLiteral(R"({"error":0,"msg_id":"%1"})")
                                 .arg(QString::fromLatin1(kMsg1)).toUtf8());
                    return;
                }

                if (text_frames == 2)
                {
                    // 第二条（"A"）：先故意压 300ms 不答，这期间不能出现第三条 1013
                    QTimer::singleShot(300, &app, [&]()
                                           {
                                               check(server.countOf(kTextMsgReq) == 2,
                                                     QStringLiteral(
                                                         "串行发送：上一条没回执时第二条不会上线路"));
                                               server.reply(kTextMsgRsp,
                                                            QStringLiteral(
                                                                R"({"error":0,"msg_id":"%1"})")
                                                            .arg(QString::fromLatin1(kMsg2)).toUtf8());
                                           });
                    return;
                }

                if (text_frames == 3)
                {
                    // 第三条（"B"）：回一个业务错误（也要回显 msg_id），并顺手推一条 1022
                    check(server.countOf(kTextMsgReq) == 3,
                          QStringLiteral("第一条回执之后第二条才发出（顺序未乱）"));
                    server.reply(kTextMsgRsp,
                                 QStringLiteral(R"({"error":1020,"msg_id":"%1"})")
                                 .arg(QString::fromLatin1(kMsg3)).toUtf8());
                    server.reply(kNotifyReceiveMessage, R"({"error":0})");
                    return;
                }

                return;
            }
        };

    QObject::connect(&socket, &mychat::ChatSocket::connected, &app, [&]()
                         {
                             writeLine(stdout, QStringLiteral("[OK] 已连接假 ChatServer，开始登录"));
                             step_timer.start(kStepTimeoutMs);
                             socket.login(666672, QStringLiteral("fake-token"));
                         });

    QObject::connect(&socket, &mychat::ChatSocket::loginFailed, &app, [&](const int error)
                         {
                             fail(QStringLiteral("登录假服务器失败, error=%1").arg(error));
                             app.exit(3);
                         });

    QObject::connect(&socket, &mychat::ChatSocket::loginSucceeded, &app,
                     [&](const std::uint64_t uid, const QString& name, const QString&)
                         {
                             Q_UNUSED(name)
                             check(uid == 666672, QStringLiteral("登录成功，uid 正确"));
                             step_timer.start(kStepTimeoutMs);
                             socket.sendTextMessage(QString::fromLatin1(kMsg1),
                                                    QString::fromLatin1(kPeerUid),
                                                    QStringLiteral("你好"));
                         });

    QObject::connect(&socket, &mychat::ChatSocket::textMessageResult, &app,
                     [&](const bool ok, const int error, const QString& msgId,
                         const QString& toUid, const QString& body)
                         {
                             ++text_result_count;
                             step_timer.start(kStepTimeoutMs);

                             if (text_result_count == 1)
                             {
                                 check(ok && error == 0,
                                       QStringLiteral("第 1 条（你好）→ error=0 上报成功"));
                                 check(msgId == QString::fromLatin1(kMsg1),
                                       QStringLiteral("回执按 msg_id 精确认领（服务端原样回显）"));
                                 check(toUid == QString::fromLatin1(kPeerUid),
                                       QStringLiteral("回执带着请求时的 to_uid 一起回来"));
                                 check(body == QStringLiteral("你好"),
                                       QStringLiteral("回执带着请求时的 body 一起回来"));

                                 // 连发两条：验证串行（第二条必须等第一条回执）
                                 socket.sendTextMessage(QString::fromLatin1(kMsg2),
                                                        QString::fromLatin1(kPeerUid),
                                                        QStringLiteral("A"));
                                 socket.sendTextMessage(QString::fromLatin1(kMsg3),
                                                        QString::fromLatin1(kPeerUid),
                                                        QStringLiteral("B"));
                                 return;
                             }

                             if (text_result_count == 2)
                             {
                                 check(ok && error == 0 && body == QStringLiteral("A")
                                         && msgId == QString::fromLatin1(kMsg2),
                                       QStringLiteral("第 2 条（A）→ 成功，msg_id 对得上"));
                                 return;
                             }

                             if (text_result_count == 3)
                             {
                                 check(!ok && error == kInvalidValueErrorCode
                                         && body == QStringLiteral("B")
                                         && msgId == QString::fromLatin1(kMsg3),
                                       QStringLiteral(
                                           "第 3 条（B）→ error=1020 原样透出，状态标记为失败"));
                                 return;
                             }

                             if (text_result_count == 4)
                             {
                                 check(!ok && error == kTimeoutErrorCode
                                         && body == QStringLiteral("静默")
                                         && msgId == QString::fromLatin1(kMsg4),
                                       QStringLiteral(
                                           "服务端不回包 → 5s 后报 RequestTimeout，不会永远卡在发送中"));

                                 writeLine(stdout, fail_count == 0
                                               ? QStringLiteral("\n[PASS] 发送链路全部通过")
                                               : QStringLiteral("\n[FAIL] 共 %1 项失败")
                                               .arg(fail_count));
                                 app.exit(fail_count == 0 ? 0 : 3);
                             }
                         });

    QObject::connect(&socket, &mychat::ChatSocket::messageReceived, &app, [&]()
                         {
                             check(!notify_received, QStringLiteral("收到 1022 通知（只表示有新消息）"));
                             notify_received = true;

                             // 最后一项：服务端只收不答，验证客户端自己会超时收尾
                             server.mute = true;
                             step_timer.start(kStepTimeoutMs);
                             socket.sendTextMessage(QString::fromLatin1(kMsg4),
                                                    QString::fromLatin1(kPeerUid),
                                                    QStringLiteral("静默"));
                         });

    overall.start(kOverallTimeoutMs);
    step_timer.start(kStepTimeoutMs);
    QTimer::singleShot(0, &app, [&]()
                           {
                               socket.connectToServer(QStringLiteral("127.0.0.1"), server.port());
                           });

    const int result = app.exec();
    writeLine(stdout, QStringLiteral("[exit] %1").arg(result));
    return result;
}
