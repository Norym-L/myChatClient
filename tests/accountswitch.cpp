// 账号切换自检 —— 同一个进程里「登录 A → 退出 → 登录 B」。
//
// 钉住一件事：**换账号后不能残留上一个账号的会话数据**。
// 之前的症状：先登录 A（会话里有 B），退出再登录 B，会话列表里会出现一条
// peer_uid == B 的会话 —— 也就是"自己和自己聊天"，摘要还是 A↔B 那一轮的内容。
// 根因是 ChatController::openStore() 只换了库句柄，没清内存里的会话 / 历史。
//
// 用法（需要真实服务端）：
//   qtClientAccountSwitchTest <A_uid> <A_pwd> <B_uid> <B_pwd> [gateUrl]
//
// 退出码：0 通过；2 参数错误；3 断言失败；4 超时。

#include <QCoreApplication>
#include <QLatin1Char>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <cstdio>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "ChatController.h"
#include "HttpClient.h"
#include "LoginController.h"
#include "logger.h"

namespace
{
constexpr int kTimeoutMs = 40000;

void writeLine(FILE* stream, const QString& text)
{
    const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
    std::fflush(stream);
}

bool containsPeer(const QVariantList& conversations, const QString& uid)
{
    for (const QVariant& entry: conversations)
    {
        if (entry.toMap().value(QStringLiteral("uid")).toString() == uid)
            return true;
    }
    return false;
}
} // namespace

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif

    mychat::log::init();
    QCoreApplication app(argc, argv);

    if (argc < 5)
    {
        writeLine(stderr,
                  QStringLiteral("用法: %1 <A_uid> <A_pwd> <B_uid> <B_pwd> [gateUrl]")
                 .arg(QString::fromUtf8(argv[0])));
        return 2;
    }

    const QString uid_a = QString::fromUtf8(argv[1]);
    const QString pwd_a = QString::fromUtf8(argv[2]);
    const QString uid_b = QString::fromUtf8(argv[3]);
    const QString pwd_b = QString::fromUtf8(argv[4]);
    const QString gate = argc >= 6 ? QString::fromUtf8(argv[5])
                                   : QStringLiteral("http://127.0.0.1:8080");

    mychat::HttpClient::instance().setBaseUrl(gate);

    // ChatController 必须先构造：它要在 ChatSocket::loginSucceeded 上抢到
    // 先注册的位置，openStore() 才会在 LoginController 发 loginSucceeded 之前跑完
    ChatController chat;
    LoginController login;

    int fail_count = 0;
    const auto check = [&](const bool ok, const QString& what)
        {
            if (ok)
            {
                writeLine(stdout, QStringLiteral("[OK] %1").arg(what));
                return;
            }
            writeLine(stderr, QStringLiteral("[FAIL] %1").arg(what));
            ++fail_count;
        };

    writeLine(stdout, QStringLiteral("A = %1    B = %2    gate = %3").arg(uid_a, uid_b, gate));

    QTimer overall;
    overall.setSingleShot(true);
    QObject::connect(&overall, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 整体超时"));
                             app.exit(4);
                         });
    overall.start(kTimeoutMs);

    int stage = 0;

    QObject::connect(&login, &LoginController::loginFailed, &app, [&](const QString& why)
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 登录失败: %1").arg(why));
                             app.exit(3);
                         });

    QObject::connect(&login, &LoginController::loginSucceeded, &app, [&]()
                         {
                             const QVariantList list = chat.conversations();

                             if (stage == 0)
                             {
                                 writeLine(stdout, QStringLiteral("\n── A(%1) 已登录，会话 %2 条 ──")
                                          .arg(uid_a).arg(list.size()));
                                 check(containsPeer(list, uid_b),
                                       QStringLiteral("A 的会话里有 B（本地历史回填正常）"));

                                 stage = 1;
                                 login.logout();
                                 // 等一下让断线 / 清理跑完，再登 B
                                 QTimer::singleShot(800, &app, [&]()
                                                        {
                                                            login.login(uid_b, pwd_b);
                                                        });
                                 return;
                             }

                             writeLine(stdout, QStringLiteral("\n── B(%1) 已登录，会话 %2 条 ──")
                                      .arg(uid_b).arg(list.size()));

                             for (const QVariant& entry: list)
                             {
                                 const QVariantMap item = entry.toMap();
                                 writeLine(stdout, QStringLiteral("   会话 uid=%1 name=%2")
                                          .arg(item.value(QStringLiteral("uid")).toString(),
                                               item.value(QStringLiteral("displayName")).toString()));
                             }

                             check(!containsPeer(list, uid_b),
                                   QStringLiteral("B 的会话里没有「自己」（换账号已清干净）"));
                             check(containsPeer(list, uid_a),
                                   QStringLiteral("B 的会话里有 A（B 自己的本地历史）"));

                             if (fail_count == 0)
                                 writeLine(stdout, QStringLiteral("\n[PASS] 账号切换不残留旧会话"));
                             app.exit(fail_count == 0 ? 0 : 3);
                         });

    QTimer::singleShot(0, &app, [&]()
                           {
                               login.login(uid_a, pwd_a);
                           });

    const int result = app.exec();
    writeLine(stdout, QStringLiteral("[exit] %1").arg(result));
    return result;
}
