#include "global.h"

#include <QApplication>
#include <QFile>
#include <QPalette>

#include "logger.h"

QString GATE_URL_PREFIX = "";

inline std::function<void(QWidget *)> repolish = [](QWidget * w)
{
    w->style()->unpolish(w);
    w->style()->polish(w);
};

namespace
{
AppTheme g_currentTheme = AppTheme::Dark;

/// 读取一份 qss 资源，失败记 WARN 并返回空串（不中断，与其它资源解耦）
QString readQssFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QFile::ReadOnly))
	{
		LOG_WARN() << "[ui] stylesheet open failed:" << path;
		return {};
	}
	return QString::fromUtf8(file.readAll());
}
} // namespace

void applyTheme(const AppTheme theme)
{
	g_currentTheme = theme;

	const QString theme_path = (theme == AppTheme::Light)
		? QStringLiteral(":/style/themes/light.qss")
		: QStringLiteral(":/style/themes/dark.qss");

	const QString style = readQssFile(QStringLiteral(":/style/common.qss"))
		+ readQssFile(theme_path);

	if (style.isEmpty())
	{
		LOG_ERROR() << "[ui] no stylesheet loaded, theme switch aborted";
		return;
	}

	// 占位文字色 QSS 管不到，只能走调色板
	QPalette palette = qApp->palette();
	palette.setColor(QPalette::PlaceholderText,
		theme == AppTheme::Light ? QColor(172, 172, 172) : QColor(130, 130, 130));
	qApp->setPalette(palette);

	qApp->setStyleSheet(style);
	LOG_INFO() << "[ui] theme applied:"
		<< (theme == AppTheme::Light ? "light" : "dark")
		<< "," << style.size() << "bytes";
}

AppTheme currentTheme()
{
	return g_currentTheme;
}

QString themedIconPath(const QString& name)
{
	if (g_currentTheme == AppTheme::Light)
	{
		return QStringLiteral(":/assets/images/themes/light/") + name + QStringLiteral(".png");
	}
	return QStringLiteral(":/res/") + name + QStringLiteral(".png");
}

namespace ChatProtocol
{
	std::optional<std::uint16_t> toMessageId(const ReqId req_id)
	{
		switch (req_id)
		{
		case ReqId::ID_CHAT_LOGIN:
			return MESSAGE_CHAT_LOGIN;
		case ReqId::ID_CHAT_LOGIN_RESP:
			return MESSAGE_CHAT_LOGIN_RESP;
		default:
			return std::nullopt;
		}
	}

	std::optional<ReqId> toReqId(const std::uint16_t message_id)
	{
		switch (message_id)
		{
		case MESSAGE_CHAT_LOGIN:
			return ReqId::ID_CHAT_LOGIN;
		case MESSAGE_CHAT_LOGIN_RESP:
			return ReqId::ID_CHAT_LOGIN_RESP;
		default:
			return std::nullopt;
		}
	}
} // namespace ChatProtocol

QString errorCodeToTip(const int error_code)
{
	switch (error_code)
	{
	case ErrorCodes::ERROR_JSON:
		return QObject::tr("请求参数错误");
	case ErrorCodes::RPC_FAILED:
		return QObject::tr("服务器繁忙，请稍后重试");
	case ErrorCodes::VERIFY_CODE_EXPIRED:
		return QObject::tr("验证码不存在或已过期，请重新获取");
	case ErrorCodes::VERIFY_CODE_ERROR:
		return QObject::tr("验证码错误");
	case ErrorCodes::USER_EXIST:
		return QObject::tr("该用户已存在");
	case ErrorCodes::PASSWORD_ERROR:
		return QObject::tr("账号或密码错误");
	case ErrorCodes::EMAIL_NOT_MATCH:
		return QObject::tr("账号与邮箱不匹配");
	case ErrorCodes::PASSWORD_UP_FAILED:
		return QObject::tr("密码修改失败，请稍后重试");
	case ErrorCodes::PASSWORD_INVALID:
		return QObject::tr("密码不符合要求");
	case ErrorCodes::REGISTER_USER_FAILED:
		return QObject::tr("注册失败，该邮箱可能已被注册");
	case ErrorCodes::USER_UID_INVALID:
		return QObject::tr("用户不存在或登录状态已失效");
	case ErrorCodes::USER_TOKEN_INVALID:
		return QObject::tr("登录凭据已失效，请重新登录");
	case ErrorCodes::SERVICE_UNAVAILABLE:
		return QObject::tr("服务器暂时不可用，请稍后重试");
	case ErrorCodes::ERR_JSON:
		return QObject::tr("服务器返回数据解析失败");
	case ErrorCodes::ERR_NETWORK:
		return QObject::tr("网络连接异常，请检查网络后重试");
	case ErrorCodes::ERR_HTTP_STATUS:
		return QObject::tr("服务器接口异常，请稍后重试");
	case ErrorCodes::ERR_REQUEST_TOO_LONG:
		return QObject::tr("提交的数据过大，请缩短后重试");
	default:
		return QObject::tr("未知错误(%1)").arg(error_code);
	}
}
