#include "logger.h"

#include <QString>
#include <QTime>

#include <atomic>
#include <cstdio>
#include <cstdlib>

#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace
{
	using chatclient::log::Level;

	/// 最低输出级别：Debug 构建全开，Release 构建为 Info
	std::atomic<int> g_min_level{
#if defined(CHATCLIENT_LOG_ENABLED)
		static_cast<int>(Level::Debug)
#else
		static_cast<int>(Level::Info)
#endif
	};

	/// Qt 消息类型 -> 日志级别
	[[nodiscard]] Level levelOfType(const QtMsgType type) noexcept
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

	/// 只保留文件名，去掉完整路径
	[[nodiscard]] QString shortFileName(const char* file)
	{
		if (file == nullptr)
		{
			return QStringLiteral("<unknown>");
		}

		const QString path = QString::fromUtf8(file);
		const int split = qMax(path.lastIndexOf(QLatin1Char('/')), path.lastIndexOf(QLatin1Char('\\')));
		return split >= 0 ? path.mid(split + 1) : path;
	}

	/**
	 * @brief write 同时输出到控制台（stderr）与调试器（Windows OutputDebugString）
	 * @note 两条通道都写，保证从 Visual Studio 启动（输出窗口）和从终端启动（标准错误）
	 *       都能看到日志；没有调试器时 OutputDebugString 是空操作，没有控制台时写 stderr 静默失败
	 */
	void write(const QString& line)
	{
		const QByteArray utf8 = line.toUtf8();
		std::fwrite(utf8.constData(), 1, static_cast<std::size_t>(utf8.size()), stderr);
		std::fputc('\n', stderr);
		std::fflush(stderr);

#ifdef Q_OS_WIN
		OutputDebugStringW(reinterpret_cast<const wchar_t*>(line.utf16()));
		OutputDebugStringW(L"\n");
#endif
	}

	/// 消息处理器：低于阈值的丢弃，其余按富格式输出
	void messageHandler(const QtMsgType type, const QMessageLogContext& context, const QString& message)
	{
		const Level level = levelOfType(type);
		if (static_cast<int>(level) >= g_min_level.load(std::memory_order_relaxed))
		{
			const QString location = context.file != nullptr
				? QStringLiteral("%1:%2").arg(shortFileName(context.file)).arg(context.line)
				: QStringLiteral("<unknown>");

			write(QStringLiteral("[%1] [%2] %3  %4")
				.arg(QTime::currentTime().toString(QStringLiteral("HH:mm:ss.zzz")),
					QString::fromLatin1(chatclient::log::levelName(level)).leftJustified(5),
					location,
					message));
		}

		if (type == QtFatalMsg)
		{
			// 自定义处理器接管后 qFatal 不会再自动终止进程，这里补齐；
			// 且不受级别过滤影响——致命错误不能因为"日志关掉了"就继续跑
			std::abort();
		}
	}
} // namespace

namespace chatclient::log
{
	void init()
	{
		qInstallMessageHandler(&messageHandler);

		// 首行固定输出当前生效的级别策略，便于确认构建配置是否符合预期
		LOG_INFO() << "[log] logger ready, min level:" << levelName(minLevel())
			<< "debug enabled:" << (debugEnabled() ? "true" : "false");
	}

	void setMinLevel(const Level level)
	{
		g_min_level.store(static_cast<int>(level), std::memory_order_relaxed);
	}

	Level minLevel()
	{
		return static_cast<Level>(g_min_level.load(std::memory_order_relaxed));
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
		return "UNKNOWN";
	}
} // namespace chatclient::log
