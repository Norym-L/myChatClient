// 登录链路自检 —— 无 GUI，直接驱动 LoginController 跑完
// 「本地校验 → HTTP 登录 → 连接 ChatServer → TCP 登录握手」。
//
// 用的是**同一个 LoginController**，所以验证的就是应用里那条真实链路，
// 不存在"测试另写一套逻辑"的问题。
//
// 用法：
//   qtClientLoginTest <uid> <password> [gateUrl]
// 例：
//   qtClientLoginTest 666672 Test@12345
//   qtClientLoginTest 666672 Test@12345 http://127.0.0.1:8080
//
// 退出码：0 成功；2 参数错误；3 登录失败；4 超时。

#include <QCoreApplication>
#include <QLatin1Char>
#include <QString>
#include <QTimer>

#include <cstdio>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "HttpClient.h"
#include "LoginController.h"
#include "logger.h"

namespace
{
    constexpr int kTimeoutMs = 20000;

    // Qt 6 里 qPrintable() 等价于 QString::toLocal8Bit()，在中文 Windows 上产出的是 **GBK**
    // 字节；而下面把控制台切成了 CP_UTF8，两者不匹配 → 中文全变乱码。
    // 统一走显式 toUtf8()，并且让 QByteArray 在整个 fwrite 期间存活
    //（写成 QString::toUtf8().constData() 会拿到悬垂指针）。
    void writeLine(FILE* stream, const QString& text)
    {
        const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
        std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
        std::fflush(stream); // 同时刷，避免 stdout / stderr 交错时顺序错乱
    }
} // namespace

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    // 控制台默认用本地代码页（GBK）解读字节，中文与 "·" 等符号会变乱码。
    // 切到 UTF-8 后程序输出的 UTF-8 才能正常显示。
    SetConsoleOutputCP(CP_UTF8);
#endif

    mychat::log::init();

    QCoreApplication app(argc, argv);

    if (argc < 3)
    {
        std::fprintf(stderr,
                     "用法: %s <uid> <password> [gateUrl]\n"
                     "例:   %s 666672 Test@12345 http://127.0.0.1:8080\n",
                     argv[0], argv[0]);
        return 2;
    }

    const QString gate_url = (argc >= 4)
                                 ? QString::fromLocal8Bit(argv[3])
                                 : QStringLiteral("http://127.0.0.1:8080");
    mychat::HttpClient::instance().setBaseUrl(gate_url);

    writeLine(stdout, QStringLiteral("gate server = %1").arg(gate_url));
    writeLine(stdout, QStringLiteral("account     = %1").arg(QString::fromUtf8(argv[1])));

    LoginController controller;

    // 过程提示（含错误信息）直接打到 stderr，便于排查
    QObject::connect(&controller, &LoginController::tipChanged, [&controller]()
    {
        writeLine(stderr, QStringLiteral("[tip%1] %2")
                  .arg(controller.tipIsError()
                           ? QStringLiteral("/err")
                           : QString(),
                       controller.tip()));
    });

    int exit_code = 3;

    QObject::connect(&controller, &LoginController::loginSucceeded, [&controller, &exit_code]()
    {
        writeLine(stdout, QStringLiteral("\n[OK] 登录成功：%1").arg(controller.userLabel()));
        exit_code = 0;
        QCoreApplication::quit();
    });

    // 失败即结束：没有这条的话，登录失败（比如密码错）要干等到超时才出结果
    QObject::connect(&controller, &LoginController::loginFailed,
                     [&exit_code](const QString& reason)
                         {
                             writeLine(stderr, QStringLiteral("\n[FAIL] %1").arg(reason));
                             exit_code = 3;
                             QCoreApplication::quit();
                         });

    // 超时保护：服务端不可达时不要挂死
    QTimer::singleShot(kTimeoutMs, &app, [&exit_code]()
    {
        writeLine(stderr, QStringLiteral("\n[FAIL] 超时（%1 ms 内未完成登录链路）").arg(kTimeoutMs));
        exit_code = 4;
        QCoreApplication::quit();
    });

    controller.login(QString::fromUtf8(argv[1]), QString::fromUtf8(argv[2]));

    app.exec();

    writeLine(stdout, QStringLiteral("[exit] %1").arg(exit_code));
    return exit_code;
}
