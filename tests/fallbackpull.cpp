// 兜底拉取自检（没有任何 1022 时，客户端也应按固定周期主动发 1023）
// —— 自带假 ChatServer，不需要真实服务端。
//
// 为什么需要这层兜底：正常链路是「服务端推 1022 → 客户端 2s 合并 → 发 1023 拉」。
// 但推送本身可能静默失效（2026-10-23 修掉的服务端竞态：旧连接断开误删新会话映射，
// 用户明明在线、1022 却被丢掉）。如果客户端只在收到 1022 时才拉，就会表现成
// 「能发不能收、重登才恢复」。
//
// 本自检钉住：
//   1. 未登录时不发 1023（定时器只在登录成功后启动）；
//   2. 登录后即使一条 1022 都没有，也会每 30s 主动拉一次（测试用
//      MYCHAT_FALLBACK_PULL_MS 缩短到 400ms，不会真等 30s）；
//   3. 兜底拉取同样遵守 count = 10 的上限。
//
// 用法（无需服务端、无需网络）：
//   qtClientFallbackPullTest
//
// 退出码：0 全部通过；3 某一步断言失败；4 整体超时；5 起不了本地监听。

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <utility>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "ChatController.h"
#include "ChatSocket.h"
#include "logger.h"

namespace
{
constexpr int kStepTimeoutMs = 10000;
constexpr int kOverallTimeoutMs = 15000;

constexpr std::uint16_t kLoginReq = 1001;
constexpr std::uint16_t kLoginRsp = 1002;
constexpr std::uint16_t kSyncChatMessageReq = 1023;
constexpr std::uint16_t kSyncChatMessageRsp = 1024;

const char* kSelfUid = "666672";

void writeLine(FILE* stream, const QString& text)
{
    const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
    std::fflush(stream);
}

/// 假 ChatServer：只实现登录和拉取两帧（本测试不推 1022）
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
} // namespace

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif

    QCoreApplication app(argc, argv);
    mychat::log::init();

    // 隔离本地库（自检不该写用户真实数据目录）；不用 %TEMP%，受限环境里写不进去。
    const QString data_dir = QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("test-data/mychat-fallback-pull-test"));
    QDir(data_dir).removeRecursively();
    qputenv("MYCHAT_DATA_DIR", data_dir.toUtf8());

    // 把 30s 兜底周期缩短到 400ms：测的是"定时器在跑、周期到了就拉"，不是等 30s。
    qputenv("MYCHAT_FALLBACK_PULL_MS", "400");

    FakeServer server;
    if (!server.listen())
    {
        writeLine(stderr, QStringLiteral("[FAIL] 起不了本地假服务器"));
        return 5;
    }
    writeLine(stdout, QStringLiteral("[setup] 假 ChatServer 已就绪，端口 %1").arg(server.port()));

    // ⚠ 控制器必须先建：它要在测试自己的 lambda 之前连上 loginSucceeded，
    //   才能保证登录后的主动拉取 / 兜底定时器由它启动。
    ChatController chat;
    auto& socket = mychat::ChatSocket::instance();

    int fail_count = 0;
    const auto check = [&](const bool ok, const QString& what)
        {
            if (ok)
            {
                writeLine(stdout, QStringLiteral("[OK] %1").arg(what));
                return true;
            }

            writeLine(stderr, QStringLiteral("[FAIL] %1").arg(what));
            ++fail_count;
            return false;
        };

    QTimer step_timer;
    step_timer.setSingleShot(true);
    QObject::connect(&step_timer, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 这一步 10s 内没有结果"));
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
    int pull_count = 0;     // 收到过几次 1023
    bool login_done = false;
    QStringList pull_counts; // 每次 1023 的 count 字段

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
                pull_counts.append(request.value(QStringLiteral("count")).toString());

                // PEL 是空的：兜底拉取和普通拉取在服务端看来没有区别
                QJsonObject rsp;
                rsp.insert(QStringLiteral("error"), 0);
                rsp.insert(QStringLiteral("count"), QStringLiteral("0"));
                server.replyJson(kSyncChatMessageRsp, rsp);
                return;
            }
        };

    QObject::connect(&socket, &mychat::ChatSocket::connected, &app, [&]()
                         {
                             writeLine(stdout, QStringLiteral("[OK] 已连接假 ChatServer"));

                             /*
                              * 先空等 700ms 再登录：未登录时定时器不该启动，
                              * 也不该有 1023（requestPull 在 isLoggedIn 前就早退）。
                              */
                             QTimer::singleShot(700, &app, [&]()
                                                    {
                                                        check(pull_count == 0,
                                                              QStringLiteral("未登录时不会发 1023"));
                                                        socket.login(666672,
                                                                     QStringLiteral("fake-token"));
                                                    });
                         });

    QObject::connect(&socket, &mychat::ChatSocket::loginSucceeded, &app,
                     [&](std::uint64_t, const QString&, const QString&)
                         {
                             step_timer.start(kStepTimeoutMs);
                             login_done = true;
                         });

    // 登录在 700ms 左右发生；这里在 2.6s 时检查，期间约 1.9s 的兜底窗口
    // （400ms 周期）足够跑出 2 次以上。给事件循环留余量，不卡死一两次抖动。
    QTimer::singleShot(2600, &app, [&]()
                           {
                               check(login_done, QStringLiteral("登录成功"));
                               check(pull_count >= 3,
                                     QStringLiteral("没有任何 1022 时也会主动拉取（实测 %1 次）")
                                     .arg(pull_count));

                               bool all_full = !pull_counts.isEmpty();
                               for (const QString& count: std::as_const(pull_counts))
                               {
                                   if (count != QStringLiteral("10"))
                                       all_full = false;
                               }
                               check(all_full, QStringLiteral("兜底拉取的 count 也是 10（服务端上限）"));

                               QDir(data_dir).removeRecursively();
                               writeLine(stdout, fail_count == 0
                                             ? QStringLiteral("\n[PASS] 兜底拉取全部通过")
                                             : QStringLiteral("\n[FAIL] 共 %1 项失败").arg(fail_count));
                               app.exit(fail_count == 0 ? 0 : 3);
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
