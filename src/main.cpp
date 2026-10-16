// qtClient —— MyChat 客户端入口（Qt Quick + ZmdUI）
//
// 设计要点：
//   1. QGuiApplication 而不是 QApplication —— 纯 QML 应用不需要 QtWidgets。
//   2. 强制 QtQuick.Controls 的 Basic 样式：ZmdUI 完全自定义了
//      background / contentItem / indicator，平台原生样式介入会出现圆角、
//      蓝色高亮等与设计语言冲突的绘制。
//   3. QML 全部打进 qrc，import 路径为 "qrc:/qml"，组件库模块根是
//      ":/qml/ZmdUI"（内含 qmldir）。
//   4. 本工程**完全自包含**：网络 / 协议 / 日志都在 ./core 下，不引用外部文件。

#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QTimer>
#include <QUrl>

#include <QtQml>

#include "ContactController.h"
#include "HttpClient.h"
#include "LoginController.h"
#include "RegisterController.h"
#include "ResetPasswordController.h"
#include "logger.h"

namespace
{
/// 文档：GateServer 默认监听 0.0.0.0:8080
constexpr char DEFAULT_GATE_HOST[] = "127.0.0.1";
constexpr char DEFAULT_GATE_PORT[] = "8080";

/// 读取可执行文件目录下的 config.ini，设置 GateServer 地址
void initGateUrl()
{
    const QString config_path = QDir::toNativeSeparators(
        QCoreApplication::applicationDirPath() + QDir::separator() +
        QStringLiteral("config.ini"));
    MCLOG_DEBUG() << "[config] read from:" << config_path;

    const QSettings settings(config_path, QSettings::IniFormat);

  QString gate_host =
        settings.value(QStringLiteral("GateServer/host")).toString().trimmed();
    QString gate_port =
        settings.value(QStringLiteral("GateServer/port")).toString().trimmed();

    if (gate_host.isEmpty() || gate_port.isEmpty()) {
    MCLOG_ERROR() << "[config] GateServer host/port missing in" << config_path
                  << ", fallback to" << DEFAULT_GATE_HOST << ":"
                  << DEFAULT_GATE_PORT;
    gate_host = gate_host.isEmpty() ? QString::fromLatin1(DEFAULT_GATE_HOST)
                                    : gate_host;
    gate_port = gate_port.isEmpty() ? QString::fromLatin1(DEFAULT_GATE_PORT)
                                    : gate_port;
  }

  mychat::HttpClient::instance().setBaseUrl(
        QStringLiteral("http://") + gate_host + QStringLiteral(":") + gate_port);
}
} // namespace

int main(int argc, char* argv[])
{
    // 尽早安装日志处理器，让 Qt 自身的启动信息也走统一格式
    mychat::log::init();

    // 高 DPI：不取整缩放系数，交给 Qt 按屏幕实际 DPR 渲染
  QGuiApplication::setHighDpiScaleFactorRoundingPolicy(
      Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);

  const QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("MyChat"));
    QGuiApplication::setOrganizationName(QStringLiteral("MyChat"));
    QGuiApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    // ZmdUI 完全自定义绘制的前提：禁用平台原生样式
  QQuickStyle::setStyle(QStringLiteral("Basic"));

  initGateUrl();

    // 控制器：各条链路分别封在各自内部，QML 只调方法、读属性、接信号。
    // 注意声明顺序 —— 它们必须比 engine 活得久（engine 先析构）。
    LoginController login_controller;
    RegisterController register_controller;
    ResetPasswordController reset_controller;
    ContactController contact_controller;

    qmlRegisterSingletonInstance("MyChat.Core", 1, 0, "LoginController",
                               &login_controller);
  qmlRegisterSingletonInstance("MyChat.Core", 1, 0, "RegisterController",
                               &register_controller);
  qmlRegisterSingletonInstance("MyChat.Core", 1, 0, "ResetPasswordController",
                               &reset_controller);
  qmlRegisterSingletonInstance("MyChat.Core", 1, 0, "ContactController",
                               &contact_controller);

  QQmlApplicationEngine engine;

    // 让 QML 里的 "import ZmdUI 1.0" 能在 qrc 里解析到 :/qml/ZmdUI/qmldir
    engine.addImportPath(QStringLiteral("qrc:/qml"));

    // 根对象创建失败时直接退出，避免留一个空窗口
  QObject::connect(
      &engine, &QQmlApplicationEngine::objectCreationFailed, &app,
      []() { QCoreApplication::exit(-1); }, Qt::QueuedConnection);

  engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
    if (engine.rootObjects().isEmpty())
        return -1;

    QObject *const root = engine.rootObjects().constFirst();

  // 开发 / 自动化用：MYCHAT_PAGE=<Login|Register|Reset> 指定起始页。
    // 配合 MYCHAT_SMOKE 就能对任意一个页面做无头抓图自检 —— 否则只能测到登录页。
    if (const QString start_page = qEnvironmentVariable("MYCHAT_PAGE");
        !start_page.isEmpty())
    {
        MCLOG_INFO() << "[app] MYCHAT_PAGE 已设置，起始页:" << start_page;
        root->setProperty("startPage", start_page);
        // Window 已完成构造（rootObjects 非空即代表 Component.onCompleted
        // 跑过了）， 所以这里要显式再调一次切换函数。
        QMetaObject::invokeMethod(root, "applyStartPage", Qt::QueuedConnection);
    }

    // 开发 / 自动化用：MYCHAT_SEARCH=<关键字>
  // 在进入聊天页后自动发起一次用户搜索。 配合 MYCHAT_AUTOLOGIN + MYCHAT_SMOKE
  // 可无头验证搜索结果列表（含多结果）的渲染。
  if (const QString auto_search = qEnvironmentVariable("MYCHAT_SEARCH");
      !auto_search.isEmpty()) {
    MCLOG_INFO() << "[app] MYCHAT_SEARCH 已设置，自动搜索:" << auto_search;
    root->setProperty("autoSearch", auto_search);
  }

  // 开发 / 自动化用：MYCHAT_OPENDLG=<other|self|selfedit>
    // 进入聊天页后自动打开「用户资料」弹窗。弹窗是 Popup，离屏抓图默认抓不到，
    // 没有这个开关就只能靠肉眼开程序看 —— 那不算可复核的证据。
    if (const QString auto_dialog = qEnvironmentVariable("MYCHAT_OPENDLG");
        !auto_dialog.isEmpty())
    {
        MCLOG_INFO() << "[app] MYCHAT_OPENDLG 已设置，自动打开资料弹窗:" << auto_dialog;
        root->setProperty("autoDialog", auto_dialog);
    }

    // 开发 / 自动化用：设了 MYCHAT_AUTOLOGIN="uid:password" 时自动发起一次登录。
    // 正常运行时不会走到这里；有它才能在无人值守的情况下验证
    // 「QML 调用 LoginController → 登录成功 → 切到聊天页」这条完整链路。
    const QString autologin = qEnvironmentVariable("MYCHAT_AUTOLOGIN");
    if (!autologin.isEmpty())
    {
        const qsizetype sep = autologin.indexOf(QLatin1Char(':'));
        if (sep > 0)
        {
            const QString account = autologin.left(sep);
            const QString password = autologin.mid(sep + 1);
            MCLOG_INFO() << "[app] MYCHAT_AUTOLOGIN 已设置，自动登录账号:" << account;
            QTimer::singleShot(0, &login_controller,
                               [&login_controller, account, password]()
                                   {
                                       login_controller.login(account, password);
                                   });
        }
        else
        {
            MCLOG_WARN() << "[app] MYCHAT_AUTOLOGIN 格式应为 uid:password";
        }
    }

    // ── 无头截图自检：MYCHAT_SMOKE=<输出 png 路径> 时抓一张图后退出 ─────
  //
  // 早先用 qml.exe 直接加载 tools/smoke.qml 的办法**已经不可用** —— main.qml
  // 现在 import MyChat.Core（C++ 注册的单例），纯 QML 运行时解析不到，
  // 会报 `module "MyChat.Core" is not installed`。
  //
  // 放到 exe 里反而是更硬的证据：验证的是「QML 从 qrc 加载 + C++ 单例注册生效
  // + 渲染管线真实出图」这整条链路，而不只是"这个文件能被人肉打开"。
  //
  // 退出码：0 = 抓图并保存成功；3 = 根对象不是 Window；4 = 保存失败。
  const QString smoke_out = qEnvironmentVariable("MYCHAT_SMOKE");
  if (!smoke_out.isEmpty()) {
    // 设了自动登录时多等一会儿，让「登录 → 切到聊天页」先完成再抓
    QTimer::singleShot(
        autologin.isEmpty() ? 1500 : 4000, &app, [root, smoke_out]() {
          auto *window = qobject_cast<QQuickWindow *>(root);
          if (window == nullptr) {
            MCLOG_ERROR() << "[smoke] 根对象不是 Window，无法抓图";
            QCoreApplication::exit(3);
            return;
          }

          const QImage image = window->grabWindow();
          const bool ok = !image.isNull() && image.save(smoke_out);

          MCLOG_INFO() << "[smoke]" << (ok ? "OK" : "FAILED") << "->"
              << smoke_out << image.size();
          QCoreApplication::exit(ok ? 0 : 4);
        });
  }

    return QGuiApplication::exec();
}
