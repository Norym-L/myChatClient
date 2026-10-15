// 重置密码链路自检 —— 无 GUI，直接驱动 ResetPasswordController。
//
// 复用的是**同一个 ResetPasswordController**，验证的就是应用里的真实链路。
//
// 用法：
//   qtClientResetTest <uid> <email> <newPassword> <verifyCode>
// 例：
//   qtClientResetTest 666674 a@b.com NewPass@1 123456
//
// 说明：VerifyServer 没启动时验证码请求会失败，可先手工往 Redis 写
//       `SET code_<email> <验证码>` 再跑本程序。
//
// 退出码：0 成功；2 参数错误；3 重置失败；4 超时。

#include <QCoreApplication>
#include <QLatin1Char>
#include <QString>
#include <QTimer>

#include <cstdio>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "HttpClient.h"
#include "ResetPasswordController.h"
#include "logger.h"

namespace
{
constexpr int kTimeoutMs = 30000;

// Qt 6 里 qPrintable() 等价于 toLocal8Bit()，中文 Windows 上产出 GBK 字节，
// 而控制台已切成 CP_UTF8 → 必乱码。统一显式 toUtf8()，并让 QByteArray 存活到写完。
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

    if (argc < 5)
    {
        writeLine(stderr,
                  QStringLiteral("用法: %1 <uid> <email> <newPassword> <verifyCode>")
                 .arg(QString::fromUtf8(argv[0])));
        return 2;
    }

    const QString uid = QString::fromUtf8(argv[1]);
    const QString email = QString::fromUtf8(argv[2]);
    const QString new_password = QString::fromUtf8(argv[3]);
    const QString code = QString::fromUtf8(argv[4]);

    mychat::HttpClient::instance().setBaseUrl(QStringLiteral("http://127.0.0.1:8080"));

    writeLine(stdout, QStringLiteral("uid   = %1").arg(uid));
    writeLine(stdout, QStringLiteral("email = %1").arg(email));

    ResetPasswordController controller;

    QObject::connect(&controller, &ResetPasswordController::tipChanged, [&controller]()
                         {
                             writeLine(stderr, QStringLiteral("[tip%1] %2")
                                      .arg(controller.tipIsError() ? QStringLiteral("/err") : QString(),
                                           controller.tip()));
                         });

    QObject::connect(&controller, &ResetPasswordController::validationFailed,
                     [](const QString& field)
                         {
                             writeLine(stderr, QStringLiteral("[invalid] %1").arg(field));
                         });

    int exit_code = 3;

    QObject::connect(&controller, &ResetPasswordController::resetSucceeded, [&exit_code]()
                         {
                             writeLine(stdout, QStringLiteral("\n[OK] 密码重置成功"));
                             exit_code = 0;
                             QCoreApplication::quit();
                         });

    QTimer::singleShot(kTimeoutMs, &app, [&exit_code]()
                           {
                               writeLine(stderr, QStringLiteral("\n[FAIL] 超时（%1 ms 内未完成重置）").arg(kTimeoutMs));
                               exit_code = 4;
                               QCoreApplication::quit();
                           });

    // ⚠ requestVerifyCode 会置 busy，而 resetPassword 开头有 `if (m_busy) return;`。
    // 紧挨着调用会让重置请求被**静默丢弃**，必须等 busy 落回 false 再提交。
    bool submitted = false;
    auto submitIfReady = [&]()
        {
            if (submitted || controller.busy())
                return;
            submitted = true;
            controller.resetPassword(uid, email, new_password, code);
        };

    QObject::connect(&controller, &ResetPasswordController::busyChanged, &app, submitIfReady);

    controller.requestVerifyCode(email);

    // 兜底：验证码请求若被本地校验直接挡下，busy 不会变化，busyChanged 也就不会触发
    QTimer::singleShot(1500, &app, submitIfReady);

    app.exec();

    writeLine(stdout, QStringLiteral("[exit] %1").arg(exit_code));
    return exit_code;
}
