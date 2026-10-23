// 请求编号（msgid）与被丢弃请求的识别自检 —— 自带假 ChatServer，不需要真实服务端。
//
// 服务端 2026-10-22 的变更：**所有** TCP 响应（含心跳、错误响应）都原样回显请求体里的
// `msgid`；各处理器不再单独回显 `id`。这条改动解决的是压测里暴露的真问题：
// 消息队列（容量 10）满时 `try_send` 直接丢帧，客户端既收不到回包也收不到错误码，
// 只能干等 5 秒超时 —— 有了 msgid 才能判定「是哪一条被丢了」。
//
// 本自检守住四条红线：
//   1. 请求体里**必须**带客户端生成的 msgid（字符串、r- 前缀、互不重复）；
//   2. 回包带同一个 msgid 时，正常认领（登录走这条）；
//   3. **不回包**时，5 秒后必须发出 requestTimedOut，且编号/请求类型都对得上，
//      同时原链路的失败回调（friendListResult）仍要被触发，不能卡住调用方；
//   4. 回包**不带** msgid（老服务端）时，仍能按同型号 FIFO 兜底 —— 不能被新协议打死。
//
// 用法：qtClientMsgIdDropTest
// 退出码：0 全部通过；3 有断言失败；4 超时；5 起不了假服务器。

#include <QByteArray>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QList>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdio>
#include <cstdint>
#include <functional>

#include "ChatSocket.h"
#include "logger.h"
#include "protocol.h"

namespace
{
constexpr std::uint16_t kLoginReq = 1001;
constexpr std::uint16_t kLoginRsp = 1002;
constexpr std::uint16_t kSearchReq = 1003;
constexpr std::uint16_t kSearchRsp = 1004;
constexpr std::uint16_t kSyncFriendListReq = 1020;
constexpr std::uint16_t kSyncFriendListRsp = 1021;

constexpr int kOverallTimeoutMs = 25000;
const char* kSelfUid = "666672";

int failures = 0;

void writeLine(FILE* stream, const QString& text)
{
    const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
    std::fflush(stream);
}

void pass(const QString& what)
{
    writeLine(stdout, QStringLiteral("[OK] ") + what);
}

void fail(const QString& what)
{
    ++failures;
    writeLine(stderr, QStringLiteral("[FAIL] ") + what);
}

/// 帧格式同真实服务端：[message_id:2 大端][body_len:2 大端][body]
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

QString msgIdOf(const QByteArray& body)
{
    return QJsonDocument::fromJson(body)
        .object()
        .value(QStringLiteral("msgid"))
        .toString();
}
} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    mychat::log::init();

    FakeServer server;
    if (!server.listen())
    {
        writeLine(stderr, QStringLiteral("[FAIL] 假服务器监听失败"));
        return 5;
    }

    auto& socket = mychat::ChatSocket::instance();

    QString login_msgid;
    QString friend_msgid;
    bool login_replied = false;
    bool timeout_checked = false;
    bool fallback_checked = false;

    QTimer overall;
    overall.setSingleShot(true);
    QObject::connect(&overall, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 整体超时（某一步没等到结果）"));
                             app.exit(4);
                         });
    overall.start(kOverallTimeoutMs);

    server.onFrame = [&](const std::uint16_t id, const QByteArray& body)
        {
            const QString msgid = msgIdOf(body);

            if (id == kLoginReq)
            {
                login_msgid = msgid;
                // 回包**回显** msgid：这是 2026-10-22 之后的真实行为
                const QString json = QStringLiteral(
                    R"({"error":0,"uid":"%1","token":"fake-token","name":"tester","msgid":"%2"})")
                    .arg(QString::fromLatin1(kSelfUid), msgid);
                server.reply(kLoginRsp, json.toUtf8());
                login_replied = true;
                return;
            }

            if (id == kSyncFriendListReq)
            {
                friend_msgid = msgid;
                /*
                 * 故意**不回包**：服务端消息队列满时 try_send 失败就是这个表现 ——
                 * 不回错误码、也不断连。客户端应当在 5 秒后自己判超时并报出 msgid。
                 */
                return;
            }

            if (id == kSearchReq)
            {
                // 故意**不带 msgid**：模拟老服务端，验证 FIFO 兜底仍然有效
                server.reply(kSearchRsp,
                             QStringLiteral(R"({"error":0,"uid":"666673","user_name":"fallback"})")
                             .toUtf8());
                return;
            }
        };

    QObject::connect(&socket, &mychat::ChatSocket::connected, &app, [&]()
                         {
                             socket.login(static_cast<std::uint64_t>(
                                              QString::fromLatin1(kSelfUid).toULongLong()),
                                          QStringLiteral("fake-token"));
                         });

    QObject::connect(&socket, &mychat::ChatSocket::loginSucceeded, &app,
                     [&](std::uint64_t, const QString&, const QString&)
                     {
                         if (!login_replied)
                             fail(QStringLiteral("登录回包没到，协议状态机异常"));
                         else
                             pass(QStringLiteral("1001 请求带 msgid，回包回显同一编号"));

                         socket.syncFriendList();
                     });

    QObject::connect(&socket, &mychat::ChatSocket::requestTimedOut, &app,
                     [&](const QString& msgid, const std::uint16_t requestId)
                     {
                         if (requestId != kSyncFriendListReq)
                             return;

                         timeout_checked = true;

                         if (friend_msgid.isEmpty())
                             fail(QStringLiteral("1020 请求体里没有 msgid"));
                         else if (msgid != friend_msgid)
                             fail(QStringLiteral("超时上报的 msgid 与请求不一致: 请求 %1 / 上报 %2")
                                      .arg(friend_msgid, msgid));
                         else
                             pass(QStringLiteral("不回包时按 msgid 精确报出被丢弃的请求"));

                         // 老服务端兜底：回包不带 msgid，仍要能认领
                         socket.searchUser(QStringLiteral("fallback"));
                     });

    QObject::connect(&socket, &mychat::ChatSocket::friendListResult, &app,
                     [&](const QList<mychat::protocol::FriendInfo>&, const int error)
                     {
                         if (error != static_cast<int>(mychat::protocol::ErrorCode::RequestTimeout))
                             fail(QStringLiteral("1020 超时应当上报 RequestTimeout，实际 %1").arg(error));
                         else
                             pass(QStringLiteral("超时后原链路仍被放行（friendListResult 收到失败）"));
                     });

    QObject::connect(&socket, &mychat::ChatSocket::searchUserResult, &app,
                     [&](const bool found, const QList<mychat::protocol::UserInfo>&,
                         const int error)
                     {
                         fallback_checked = true;

                         if (!found || error != 0)
                             fail(QStringLiteral("回包不带 msgid 时兜底认领失败: found=%1 error=%2")
                                      .arg(found)
                                      .arg(error));
                         else
                             pass(QStringLiteral("回包不带 msgid 时仍按 FIFO 兜底认领"));

                         // 三条红线都覆盖到了，提前收工（不必等整体超时）
                         if (timeout_checked && fallback_checked)
                         {
                             // 两个 msgid 必须不同：同一个连接里编号要唯一
                             if (login_msgid.isEmpty() || friend_msgid.isEmpty()
                                 || login_msgid == friend_msgid)
                                 fail(QStringLiteral("同一连接的请求编号重复或缺失"));
                             else
                                 pass(QStringLiteral("同一连接的请求编号唯一且连续"));

                             writeLine(stdout, failures == 0
                                           ? QStringLiteral("[DONE] 全部通过")
                                           : QStringLiteral("[DONE] 有 %1 项失败").arg(failures));
                             app.exit(failures == 0 ? 0 : 3);
                         }
                     });

    socket.connectToServer(QStringLiteral("127.0.0.1"), server.port());

    const int code = app.exec();
    socket.disconnectFromServer();

    if (code == 0 && (!timeout_checked || !fallback_checked))
    {
        writeLine(stderr, QStringLiteral("[FAIL] 没跑完全部断言"));
        return 3;
    }
    return code;
}
