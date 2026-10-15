// 注册链路自检 —— 无 GUI，直接驱动 RegisterController
// 「本地校验 → 请求验证码 → 提交注册」。
//
// 复用的是**同一个 RegisterController**，所以验证的就是应用里那条真实链路。
//
// 用法：
//   qtClientRegisterTest <email> <nickname> <password> [verifyCode]
// 例：
//   qtClientRegisterTest a@b.com norym Test@12345 123456
//
// 说明：
//   · 给了 verifyCode 时：先发一次 requestVerifyCode（顺带验证这条路径），
//     再用该码调用 registerUser。
//   · 没给时：只发 requestVerifyCode 就结束 —— 用于单独排查验证码那一步。
//   · VerifyServer 没启动的话验证码请求会失败（"there is no verify in redis!"），
//     此时可手工往 Redis 写 code_<email> 后再跑。
//
// 退出码：0 成功；2 参数错误；3 注册失败；4 超时。

#include <QCoreApplication>
#include <QLatin1Char>
#include <QString>
#include <QTimer>

#include <cstdio>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "HttpClient.h"
#include "RegisterController.h"
#include "logger.h"

namespace
{
constexpr int kTimeoutMs = 30000;

// Qt 6 里 qPrintable() 等价于 toLocal8Bit()，中文 Windows 上产出 **GBK** 字节；
// 而下面把控制台切成了 CP_UTF8，两者不匹配 → 中文全乱码。统一走显式 toUtf8()，
// 并让 QByteArray 在整个 fwrite 期间存活（写成 toUtf8().constData() 是悬垂指针）。
void writeLine(FILE* stream, const QString& text)
{
    const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
    std::fflush(stream);
}
} // namespace

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif

    mychat::log::init();

    QCoreApplication app(argc, argv);

    if (argc < 4)
    {
        writeLine(stderr,
                  QStringLiteral("用法: %1 <email> <nickname> <password> [verifyCode]")
                 .arg(QString::fromUtf8(argv[0])));
        return 2;
    }

    const QString email = QString::fromUtf8(argv[1]);
    const QString nickname = QString::fromUtf8(argv[2]);
    const QString password = QString::fromUtf8(argv[3]);
    const bool has_code = argc >= 5;
    const QString code = has_code ? QString::fromUtf8(argv[4]) : QString();

    mychat::HttpClient::instance().setBaseUrl(QStringLiteral("http://127.0.0.1:8080"));

    writeLine(stdout, QStringLiteral("email    = %1").arg(email));
    writeLine(stdout, QStringLiteral("nickname = %1").arg(nickname));

    RegisterController controller;

    QObject::connect(&controller, &RegisterController::tipChanged, [&controller]()
                         {
                             writeLine(stderr, QStringLiteral("[tip%1] %2")
                                      .arg(controller.tipIsError() ? QStringLiteral("/err") : QString(),
                                           controller.tip()));
                         });

    QObject::connect(&controller, &RegisterController::validationFailed,
                     [](const QString& field)
                         {
                             writeLine(stderr, QStringLiteral("[invalid] %1").arg(field));
                         });

    int exit_code = 3;

    QObject::connect(&controller, &RegisterController::registerSucceeded,
                     [&exit_code](const QString& uid)
                         {
                             writeLine(stdout, QStringLiteral("\n[OK] 注册成功：UID %1").arg(uid));
                             exit_code = 0;
                             QCoreApplication::quit();
                         });

    QTimer::singleShot(kTimeoutMs, &app, [&exit_code]()
                           {
                               writeLine(stderr, QStringLiteral("\n[FAIL] 超时（%1 ms 内未完成注册）").arg(kTimeoutMs));
                               exit_code = 4;
                               QCoreApplication::quit();
                           });

    // ⚠ requestVerifyCode 会置 busy，而 registerUser 开头有 `if (m_busy) return;`。
    // 两者紧挨着调用的话，注册请求会被**静默丢弃**（第一次写这个测试就踩了）。
    // 所以必须等验证码那一轮结束（busy 落回 false）再提交。
    bool submitted = false;
    auto submitIfReady = [&]()
        {
            if (submitted || !has_code || controller.busy())
                return;
            submitted = true;
            controller.registerUser(email, nickname, password, password, code);
        };

    QObject::connect(&controller, &RegisterController::busyChanged, &app, submitIfReady);

    // 先请求验证码（结果只作参考，失败也不中断 —— 验证码可能已被人工写入 Redis）
    controller.requestVerifyCode(email);

    if (!has_code)
    {
        // 没有现成的验证码，等验证码请求出结果后就收工
        QTimer::singleShot(3000, &app, [&exit_code]()
                               {
                                   writeLine(stdout, QStringLiteral("\n[DONE] 验证码请求已发出；"
                                                 "要完成注册请把验证码作为第 4 个参数传入"));
                                   exit_code = 0;
                                   QCoreApplication::quit();
                               });
    }
    else
    {
        // 兜底：验证码请求若被本地校验直接挡下，busy 不会发生变化，
        // busyChanged 就不会触发，这里延时补一刀。
        QTimer::singleShot(1500, &app, submitIfReady);
    }

    app.exec();

    writeLine(stdout, QStringLiteral("[exit] %1").arg(exit_code));
    return exit_code;
}
