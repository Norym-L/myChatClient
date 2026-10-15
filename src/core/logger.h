#pragma once

#include <QDebug>

/**
 * 日志：把 Qt 自身的消息（qDebug / qInfo / qWarning / qCritical）统一装到一个
 * 处理器上，输出格式为
 *
 *   [HH:mm:ss.zzz] [LEVEL] 消息
 *
 * 输出目标：控制台（stderr）与调试器（Windows OutputDebugString）同时输出 ——
 * 无论从终端启动还是从 IDE 启动都能看到。
 *
 * 用法：
 *   mychat::log::init();            // 进程内尽早调用一次（幂等）
 *   qInfo() << "gate server:" << url;
 *
 * 说明：Debug 级别的输出在 Release 构建里被编译期消除，见 MCLOG_DEBUG。
 */
namespace mychat::log
{
    enum class Level : int
    {
        Debug = 0,
        Info = 1,
        Warn = 2,
        Error = 3,
        /// 仅用于 setMinLevel：关闭全部输出
        Off = 4,
    };

    /// 安装消息处理器与输出格式（幂等，重复调用无副作用）
    void init();

    /// 运行时最低输出级别（低于该级别的消息一律丢弃）
    void setMinLevel(Level level);

    /// 当前最低输出级别
    [[nodiscard]] Level minLevel();

    /// 级别名称："DEBUG" / "INFO" / "WARN" / "ERROR"
    [[nodiscard]] const char* levelName(Level level);
} // namespace mychat::log

/// Debug 级日志。Release 构建下整条语句被编译期消除（参数不求值、字符串不进二进制）。
#if !defined(NDEBUG) || defined(MYCHAT_LOG_DEBUG_ENABLED)
#  define MCLOG_DEBUG() qDebug()
#else
#  define MCLOG_DEBUG() while (false) QMessageLogger().noDebug()
#endif

#define MCLOG_INFO()  qInfo()
#define MCLOG_WARN()  qWarning()
#define MCLOG_ERROR() qCritical()
