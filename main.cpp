#include "mainwindow.h"

#include <QApplication>
#include <QFile>
#include <QStringList>

#include "global.h"
#include "logger.h"

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

    // 样式表按 ui/page 拆分为多个 .qss（见 assets/styles/），此处按原
    // stylesheet.qss 的段落顺序拼接加载——顺序决定同名选择器的覆盖结果，
    // 与拆分前行为保持一致，新增页面样式请追加到列表末尾或对应位置。
    const QStringList kQssFiles = {
        ":/style/common.qss",
        ":/style/login/logindialog.qss",
        ":/style/register/registerdialog.qss",
        ":/style/reset/resetdialog.qss",
        ":/style/chat/chatpage.qss",
        ":/style/chat/chatPage/interchatpage.qss",
        ":/style/chat/interListItem/friendwidget.qss",
        ":/style/chat/interListItem/adduseritem.qss",
        ":/style/chat/findResultDialog/findresultdialog.qss",
        // friendlabel.qss 必须在 applyfriend.qss 之前：
        // 后者对 #grid 内的标签区做了底色，两处选择器不冲突，
        // 但保持「组件样式先于页面样式」的既有惯例。
        ":/style/applyFriend/friendlabel.qss",
        ":/style/applyFriend/applyfriend.qss"
    };

    QString style;
    qint64 total_size = 0;
    int loaded_count = 0;

    for (const QString &qss_path : kQssFiles)
    {
        QFile qss(qss_path);

        if (qss.open(QFile::ReadOnly))
        {
            const QByteArray data = qss.readAll();
            style.append(QString::fromUtf8(data));
            total_size += data.size();
            ++loaded_count;
            qss.close();
        }
        else
        {
            LOG_WARN() << "[ui] stylesheet open failed:" << qss.fileName();
        }
    }

    if (loaded_count > 0)
    {
        LOG_DEBUG() << "[ui] stylesheets loaded," << loaded_count << "/"
                    << kQssFiles.size() << "files," << total_size << "bytes";
        a.setStyleSheet(style);
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
