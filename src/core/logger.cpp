#include "logger.h"

#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <QMessageLogContext>
#include <QMutex>
#include <QMutexLocker>

#include <cstdio>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace mychat::log
{
    namespace
    {
        QMutex g_mutex;
        Level g_min_level = Level::Debug;
        bool g_installed = false;

#if !defined(NDEBUG)
        constexpr Level kDefaultLevel = Level::Debug;
#else
        constexpr Level kDefaultLevel = Level::Info;
#endif

        Level levelOf(const QtMsgType type)
        {
            switch (type)
            {
            case QtDebugMsg:
                return Level::Debug;
            case QtInfoMsg:
                return Level::Info;
            case QtWarningMsg:
                return Level::Warn;
            case QtCriticalMsg:
            case QtFatalMsg:
                return Level::Error;
            }
            return Level::Info;
        }

        void handler(const QtMsgType type, const QMessageLogContext& context, const QString& message)
        {
            const Level level = levelOf(type);

            {
                const QMutexLocker locker(&g_mutex);
                if (level < g_min_level)
                    return;
            }

            const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));

            // 注意：QString::arg 不吃 printf 的 "%-5s" 这种宽度语法 —— 写成 "[%-5%1]" 的话
            // "-" 不是数字，整段会被当普通文本原样输出，结果就是难看的 "[%-5INFO]"。
            // 要对齐得用 QString::leftJustified()。
            const QString line = QStringLiteral("[%1] [%2] %3")
                .arg(stamp,
                     QString::fromLatin1(levelName(level)).leftJustified(5),
                     message);

            const QByteArray utf8 = line.toUtf8();

            // 控制台与调试器同时输出：终端启动看 stderr，IDE 启动看 OutputDebugString
            std::fputs(utf8.constData(), stderr);
            std::fputc('\n', stderr);
            std::fflush(stderr);

#ifdef Q_OS_WIN
            ::OutputDebugStringA(utf8.constData());
            ::OutputDebugStringA("\n");
#endif

            if (type == QtFatalMsg)
                abort();
        }
    } // namespace

    void init()
    {
        const QMutexLocker locker(&g_mutex);
        if (g_installed)
            return;

        g_min_level = kDefaultLevel;
        qInstallMessageHandler(handler);
        g_installed = true;
    }

    void setMinLevel(const Level level)
    {
        const QMutexLocker locker(&g_mutex);
        g_min_level = level;
    }

    Level minLevel()
    {
        const QMutexLocker locker(&g_mutex);
        return g_min_level;
    }

    const char* levelName(const Level level)
    {
        switch (level)
        {
        case Level::Debug:
            return "DEBUG";
        case Level::Info:
            return "INFO";
        case Level::Warn:
            return "WARN";
        case Level::Error:
            return "ERROR";
        case Level::Off:
            return "OFF";
        }
        return "?";
    }
} // namespace mychat::log
