#ifndef CHATCLIENT_LOGGER_H
#define CHATCLIENT_LOGGER_H

/**
 * @file logger.h
 * @brief 统一日志接口
 *
 * 级别：DEBUG < INFO < WARN < ERROR
 *
 * | 构建   | 输出的级别            | LOG_DEBUG 展开结果 |
 * |--------|----------------------|--------------------|
 * | Debug  | DEBUG / INFO / WARN / ERROR（全部） | 正常记录 |
 * | Release| INFO / WARN / ERROR  | 编译期消除（参数不求值、不产生代码、字符串不进二进制） |
 *
 * 输出目标：控制台（stderr）与调试器（Windows OutputDebugString）**同时输出**，
 * 因此无论从 Visual Studio 启动（输出窗口）还是从终端启动（标准错误）都能看到日志。
 *
 * 构建类型的判定（按优先级）：
 *   1. 工程显式宏：Debug 定义 CHATCLIENT_LOG_ENABLED，Release 定义 CHATCLIENT_LOG_DISABLED
 *      （见 ChatClient.vcxproj 的 PreprocessorDefinitions）
 *   2. 两者都没有时退回编译器宏：_DEBUG（MSVC Debug）
 *   3. 不使用 QT_DEBUG / QT_NO_DEBUG：它们反映的是 Qt 库自身的构建类型，
 *      而 Qt 官方 MSVC 包是 debug + release 混装，拿它判断会与工程配置不一致
 *
 * 用法：
 *   LOG_DEBUG() << "[tcp] send message_id:" << message_id;
 *   LOG_INFO()  << "[app] gate server:" << GATE_URL_PREFIX;
 *   LOG_WARN()  << "[http] network error:" << error_string;
 *   LOG_ERROR() << "[app] fatal: xxx";
 */

/**
 * 保证任何构建下都能拿到 文件:行 上下文。
 * Qt 在 QT_NO_DEBUG 被定义时（例如 CMake 的 Release 配置会自带 -DQT_NO_DEBUG）
 * 会默认关闭 QT_MESSAGELOGCONTEXT，导致 QMessageLogContext::file 为 nullptr、
 * 日志前缀里只剩 <unknown>。这里必须在包含 Qt 头文件之前显式打开。
 * 若使用者显式定义 QT_NO_MESSAGELOGCONTEXT 表示主动放弃上下文，此处不覆盖。
 */
#if !defined(QT_MESSAGELOGCONTEXT) && !defined(QT_NO_MESSAGELOGCONTEXT)
#  define QT_MESSAGELOGCONTEXT
#endif

#include <QDebug>

/// 构建类型判定
#if !defined(CHATCLIENT_LOG_ENABLED) && !defined(CHATCLIENT_LOG_DISABLED)
#  if defined(_DEBUG) || defined(QT_DEBUG)
#    define CHATCLIENT_LOG_ENABLED
#  else
#    define CHATCLIENT_LOG_DISABLED
#  endif
#endif

/// 记录点上下文（文件/行/函数）
#define CHATCLIENT_LOG_CONTEXT() \
	QMessageLogger(QT_MESSAGELOG_FILE, QT_MESSAGELOG_LINE, QT_MESSAGELOG_FUNC)

#if defined(CHATCLIENT_LOG_ENABLED)
#  define LOG_DEBUG() CHATCLIENT_LOG_CONTEXT().debug()
#else
/**
 * Release 构建：整条语句在编译期被消除。
 * 沿用 Qt 官方的 QT_NO_QDEBUG_MACRO 形态（qlogging.h），
 * `while (false)` 保证后面的流式参数既不求值、也不生成任何代码。
 * 注意 noDebug 后面的括号不能省：本宏是函数式宏，调用处的 () 会被宏吃掉。
 */
#  define LOG_DEBUG() while (false) QMessageLogger().noDebug()
#endif

// INFO 及以上在所有构建中保留；最终是否输出由 logger 的运行时阈值把关
#define LOG_INFO()  CHATCLIENT_LOG_CONTEXT().info()
#define LOG_WARN()  CHATCLIENT_LOG_CONTEXT().warning()
#define LOG_ERROR() CHATCLIENT_LOG_CONTEXT().critical()

namespace chatclient::log
{
	/// 日志级别，数值越大越严重
	enum class Level : int
	{
		Debug = 0,
		Info = 1,
		Warn = 2,
		Error = 3,
		/// 只用于 setMinLevel：关闭全部日志输出（默认不会用到）
		Off = 4,
	};

	/**
	 * @brief init 安装消息处理器与输出格式，进程内应尽早调用一次（幂等）
	 */
	void init();

	/**
	 * @brief setMinLevel 设置运行时最低输出级别（低于该级别的消息一律丢弃）
	 * @note 默认值：Debug 构建为 Debug，Release 构建为 Info
	 *       需要"连 INFO 都不输出"时传入 Level::Off
	 *       （注意：qFatal 的进程终止不受级别过滤影响）
	 */
	void setMinLevel(Level level);

	/// 当前最低输出级别
	[[nodiscard]] Level minLevel();

	/// 级别名称（"DEBUG" / "INFO" / "WARN" / "ERROR" / "OFF"）
	[[nodiscard]] const char* levelName(Level level);

	/// 当前构建是否保留 DEBUG 级别（Release 构建恒为 false）
	[[nodiscard]] constexpr bool debugEnabled() noexcept
	{
#if defined(CHATCLIENT_LOG_ENABLED)
		return true;
#else
		return false;
#endif
	}
} // namespace chatclient::log

#endif // CHATCLIENT_LOGGER_H
