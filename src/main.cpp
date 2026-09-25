#include "ui/mainwindow.h"

#include <QApplication>
#include <QFile>

#include "app/global.h"
#include "core/logger.h"

namespace
{
/// 文档：GateServer 默认监听 0.0.0.0:8080
constexpr char DEFAULT_GATE_HOST[] = "127.0.0.1";
constexpr char DEFAULT_GATE_PORT[] = "8080";
} // namespace

int main(int argc, char *argv[])
{
    // 尽早安装日志处理器，让 Qt 自身的启动信息也走统一格式
    chatclient::log::init();

    QApplication a(argc, argv);

    QFile qss(":/style/stylesheet.qss");

    if (qss.open(QFile::ReadOnly))
    {
        LOG_DEBUG() << "[ui] stylesheet loaded, size:" << qss.size();
        QString style = QLatin1String(qss.readAll());
        a.setStyleSheet(style);
        qss.close();
    }
    else
    {
        LOG_WARN() << "[ui] stylesheet open failed:" << qss.fileName();
    }

    // 配置来源：可执行文件目录下的 config.ini
    QString file_name = "config.ini";
    QString app_path = QCoreApplication::applicationDirPath();
    QString config_path = QDir::toNativeSeparators(app_path + QDir::separator() + file_name);
    LOG_DEBUG() << "[config] read from:" << config_path;

    QSettings settings(config_path, QSettings::IniFormat);

    QString gate_host = settings.value("GateServer/host").toString().trimmed();
    QString gate_port = settings.value("GateServer/port").toString().trimmed();

    if (gate_host.isEmpty() || gate_port.isEmpty())
    {
        LOG_ERROR() << "[config] GateServer host/port missing in" << config_path
            << ", fallback to" << DEFAULT_GATE_HOST << ":" << DEFAULT_GATE_PORT;
        gate_host = gate_host.isEmpty() ? DEFAULT_GATE_HOST : gate_host;
        gate_port = gate_port.isEmpty() ? DEFAULT_GATE_PORT : gate_port;
    }

    GATE_URL_PREFIX = "http://" + gate_host + ":" + gate_port;

    LOG_INFO() << "[app] ChatClient started, gate server:" << GATE_URL_PREFIX;

    MainWindow w;
    w.show();

    return QApplication::exec();
}
