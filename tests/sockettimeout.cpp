// 回包超时自检 —— 验证"服务端一句都不回"时，客户端会不会自己走出来。
//
// 为什么需要它：服务端存在"某个分支直接 return、一句都不回"的实现
// （实测：拿不属于自己的申请去发 1010 就是这种）。TCP 层没有任何"一定
// 有回包"的保证，所以超时兜底必须存在 —— 而这种场景**没法靠真实服务端稳定复现**，
// 只能自己造一个"收包但不答话"的对端。
//
// 做法：在本进程里起一个只接受连接、永不发数据的 QTcpServer，让 ChatSocket 连它，
// 然后挨个发请求。期望每条都在 \l kReplyTimeoutMs 附近报出超时（错误码 = 超时），
// 而不是永远卡住。
//
// 用法（无需服务端、无需网络）：
//   qtClientSocketTimeoutTest
//
// 退出码：0 全部通过；3 有一条没报超时 / 报错码不对；4 整体超时（都不回来）。
//        5 连不上本地的哑服务器（环境问题，不是超时逻辑的问题）。

#include <QCoreApplication>
#include <QLatin1Char>
#include <QList>
#include <QString>
#include <QTimer>
#include <QTcpServer>
#include <QTcpSocket>

#include <functional>

#include <cstdio>
#include <cstdint>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "ChatSocket.h"
#include "logger.h"

namespace
{
// 每条留 2 秒余量（超时窗口 5s），整体再给一大段
constexpr int kPerStepTimeoutMs = 7000;
constexpr int kOverallTimeoutMs = 60000;

constexpr int kTimeoutErrorCode = 5; // protocol::ErrorCode::RequestTimeout

void writeLine(FILE* stream, const QString& text)
{
    const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
    std::fflush(stream);
}

enum class Step
{
    Connect,
    Login,
    Search,
    AddFriend,
    SyncList,
    AuthFriend,
    Done
};

QString stepName(const Step step)
{
    switch (step)
    {
        case Step::Connect: return QStringLiteral("连接哑服务器");
        case Step::Login: return QStringLiteral("登录");
        case Step::Search: return QStringLiteral("搜索用户");
        case Step::AddFriend: return QStringLiteral("发送好友申请");
        case Step::SyncList: return QStringLiteral("拉取好友申请列表");
        case Step::AuthFriend: return QStringLiteral("处理好友申请");
        case Step::Done: return QStringLiteral("完成");
    }
    return QStringLiteral("?");
}
} // namespace

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif

    QCoreApplication app(argc, argv);
    mychat::log::init();

    /*
     * 哑服务器：接受连接后**什么都不发** —— 这正是"服务端漏回包"的样子。
     * 它必须一直活着（同时摆着那个 pendingConnection），否则连接会被当成断线处理，
     * 走到 onDisconnected 分支，测的就不是超时了。
     */
    QTcpServer dummy;
    if (!dummy.listen(QHostAddress::LocalHost, 0))
    {
        writeLine(stderr, QStringLiteral("[FAIL] 起不了本地哑服务器: %1").arg(dummy.errorString()));
        return 5;
    }
    writeLine(stdout, QStringLiteral("[setup] 哑服务器已就绪，端口 %1（只收不发）")
             .arg(dummy.serverPort()));

    auto& socket = mychat::ChatSocket::instance();

    Step step = Step::Connect;
    bool step_settled = false;

    QTimer step_timer;
    step_timer.setSingleShot(true);

    QTimer overall;
    overall.setSingleShot(true);
    QObject::connect(&overall, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 整体超时，卡在「%1」")
                                      .arg(stepName(step)));
                             app.exit(4);
                         });

    /*
     * 每一步有了结果就往下推一格。先声明后赋值：advance 里要发下一条请求，
     * 而 finishStep 又要回头调 advance —— 两个互相引用，只能先占位。
     */
    std::function < void() > advance;

    // 每步的关键判据：必须报「超时」，报别的错误码等于这条链路的兜底没生效
    const std::function<void(int, Step)> finishStep = [&](const int error, const Step expected)
        {
            if (step != expected)
                return;

            step_timer.stop();
            step_settled = true;

            if (error == kTimeoutErrorCode)
            {
                writeLine(stdout, QStringLiteral("[OK] %1 → 超时被兜住 (error=%2，界面不会再转圈)")
                                 .arg(stepName(step)).arg(error));
                advance();
                return;
            }

            writeLine(stderr, QStringLiteral("[FAIL] %1 → 报的是 error=%2，期望超时(%3)")
                             .arg(stepName(step)).arg(error).arg(kTimeoutErrorCode));
            app.exit(3);
        };

    advance = [&]()
        {
            if (!step_settled)
                return;
            step_settled = false;

            switch (step)
            {
                case Step::Connect:
                    step = Step::Login;
                    socket.login(666672, QStringLiteral("dummy-token"));
                    break;
                case Step::Login:
                    step = Step::Search;
                    socket.searchUser(QStringLiteral("666673"));
                    break;
                case Step::Search:
                    step = Step::AddFriend;
                    socket.sendFriendRequest(666672, 666673, QStringLiteral("hi"), QStringLiteral("备注"));
                    break;
                case Step::AddFriend:
                    step = Step::SyncList;
                    socket.syncFriendRequests(666672);
                    break;
                case Step::SyncList:
                    step = Step::AuthFriend;
                    socket.authFriendRequest(12, 1);
                    break;
                case Step::AuthFriend:
                    step = Step::Done;
                    writeLine(stdout, QStringLiteral("\n[PASS] 所有请求都会自己超时收尾，没有一条卡死"));
                    app.exit(0);
                    return;
                case Step::Done:
                    return;
            }

            step_timer.start(kPerStepTimeoutMs);
        };

    QObject::connect(&step_timer, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr,
                                       QStringLiteral("[FAIL] %1 等了 %2 ms 也没有任何结果")
                                      .arg(stepName(step)).arg(kPerStepTimeoutMs));
                             app.exit(3);
                         });

    QObject::connect(&socket, &mychat::ChatSocket::loginFailed, &app,
                     [&](const int error)
                         {
                             finishStep(error, Step::Login);
                         });

    QObject::connect(&socket, &mychat::ChatSocket::searchUserResult, &app,
                     [&](const bool found, const QList<mychat::protocol::UserInfo>& infos,
                         const int error)
                         {
                             Q_UNUSED(found)
                             Q_UNUSED(infos)
                             finishStep(error, Step::Search);
                         });

    QObject::connect(&socket, &mychat::ChatSocket::friendRequestResult, &app,
                     [&](const bool ok, const int error, const std::uint64_t requestId)
                         {
                             Q_UNUSED(ok)
                             Q_UNUSED(requestId)
                             finishStep(error, Step::AddFriend);
                         });

    QObject::connect(&socket, &mychat::ChatSocket::friendRequestList, &app,
                     [&](const QList<mychat::protocol::FriendRequest>& requests, const int error)
                         {
                             Q_UNUSED(requests)
                             finishStep(error, Step::SyncList);
                         });

    QObject::connect(&socket, &mychat::ChatSocket::authFriendRequestResult, &app,
                     [&](const bool ok, const int error, const std::uint64_t requestId,
                         const int state)
                         {
                             Q_UNUSED(ok)
                             Q_UNUSED(requestId)
                             Q_UNUSED(state)
                             finishStep(error, Step::AuthFriend);
                         });

    QObject::connect(&socket, &mychat::ChatSocket::connected, &app, [&]()
                         {
                             writeLine(stdout, QStringLiteral("[OK] 连接哑服务器成功"));
                             step_settled = true;
                             advance();
                         });

    overall.start(kOverallTimeoutMs);
    QTimer::singleShot(0, &app, [&]()
                           {
                               socket.connectToServer(QStringLiteral("127.0.0.1"),
                                                      static_cast<std::uint16_t>(dummy.serverPort()));
                           });

    const int result = app.exec();
    writeLine(stdout, QStringLiteral("[exit] %1").arg(result));
    return result;
}
