#include "global.h"

#include <QApplication>
#include <QFile>
#include <QPalette>

#include "logger.h"

QString GATE_URL_PREFIX = "";

inline std::function<void(QWidget*)> repolish = [](QWidget* w)
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
    return req_id - 4;
}

std::optional<ReqId> toReqId(const std::uint16_t message_id)
{
    return static_cast<ReqId>(message_id + 4);
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

std::optional<std::uint64_t> parseUid(const QJsonValue& value)
{
    // 首选形态：uint64 的十进制字符串
    if (value.isString())
    {
        const QString text = value.toString();
        bool be_valid = false;
        const std::uint64_t uid = text.toULongLong(&be_valid);
        if (!be_valid)
        {
            LOG_WARN() << "[json] uid string is not a valid uint64:" << text;
            return std::nullopt;
        }
        return uid;
    }

    // 兼容形态：数字。QJsonValue 内部即 double，绝对值超过 2^53 的整数已无法精确表示，
    // 此时解析结果不可信，但为了不在灰度期直接断线仍返回，并记录告警。
    if (value.isDouble())
    {
        constexpr double kMaxExactInteger = 9007199254740992.0; // 2^53
        const double raw = value.toDouble();
        if (raw < 0.0 || raw > kMaxExactInteger)
        {
            LOG_WARN() << "[json] uid is sent as a number out of double exact range:" << raw
                << ", server should send it as a string";
        }
        return static_cast<std::uint64_t>(raw);
    }

    LOG_WARN() << "[json] uid field is missing or of unexpected type";
    return std::nullopt;
}
