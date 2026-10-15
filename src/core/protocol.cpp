#include "protocol.h"

#include <QDebug>
#include <QObject>

namespace mychat::protocol
{
    QString reqPath(const ReqId id)
    {
        switch (id)
        {
            case ReqId::GetVerifyCode:
                return QStringLiteral("/get_Verifycode");
            case ReqId::RegisterUser:
                return QStringLiteral("/user_register");
            case ReqId::ResetPassword:
                return QStringLiteral("/reset_password");
            case ReqId::LoginUser:
                return QStringLiteral("/user_login");
        }
        qWarning() << "[protocol] unknown ReqId:" << static_cast<int>(id);
        return {};
    }

    QString verifyCodeErrorToTip(const int code)
    {
        switch (static_cast<ErrorCode>(code))
        {
            case ErrorCode::Success:
                return {};
            case ErrorCode::ServerJsonError:
                return QObject::tr("请求参数错误");
            case ErrorCode::RpcFailed:
                return QObject::tr("验证码服务不可用，请确认 VerifyServer 已启动");
            default:
                // VerifyServer 透传上来的码，客户端无从翻译，如实报出来便于排查
                return QObject::tr("验证码发送失败（服务端错误码 %1）").arg(code);
        }
    }

    QString errorToTip(const int code)
    {
        switch (static_cast<ErrorCode>(code))
        {
        case ErrorCode::ServerJsonError:
            return QObject::tr("请求参数错误");
        case ErrorCode::RpcFailed:
            return QObject::tr("服务器繁忙，请稍后重试");
        case ErrorCode::VerifyCodeExpired:
            return QObject::tr("验证码不存在或已过期，请重新获取");
        case ErrorCode::VerifyCodeError:
            return QObject::tr("验证码错误");
        case ErrorCode::UserExist:
            return QObject::tr("该用户已存在");
        case ErrorCode::PasswordError:
            return QObject::tr("账号或密码错误");
        case ErrorCode::EmailNotMatch:
            return QObject::tr("账号与邮箱不匹配");
        case ErrorCode::PasswordUpdateFailed:
            return QObject::tr("密码修改失败，请稍后重试");
        case ErrorCode::PasswordInvalid:
            return QObject::tr("密码不符合要求");
        case ErrorCode::RegisterUserFailed:
            return QObject::tr("注册失败，该邮箱可能已被注册");
        case ErrorCode::UserUidInvalid:
            return QObject::tr("用户不存在或登录状态已失效");
        case ErrorCode::UserTokenInvalid:
            return QObject::tr("登录凭据已失效，请重新登录");
        case ErrorCode::ServiceUnavailable:
            return QObject::tr("服务器暂时不可用，请稍后重试");
        case ErrorCode::UserNotFound:
            return QObject::tr("用户不存在");
        case ErrorCode::JsonError:
            return QObject::tr("服务器返回数据解析失败");
        case ErrorCode::NetworkError:
            return QObject::tr("网络连接异常，请检查网络后重试");
        case ErrorCode::HttpStatusError:
            return QObject::tr("服务器接口异常，请稍后重试");
        case ErrorCode::RequestTooLong:
            return QObject::tr("提交的数据过大，请缩短后重试");
        case ErrorCode::Success:
            return {};
        default:
            return QObject::tr("未知错误(%1)").arg(code);
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
                qWarning() << "[protocol] uid string is not a valid uint64:" << text;
                return std::nullopt;
            }
            return uid;
        }

        // 兼容形态：数字。QJsonValue 内部即 double，绝对值超过 2^53 的整数已无法精确表示，
        // 此时结果不可信，但仍返回并告警，免得灰度期直接断线。
        if (value.isDouble())
        {
            constexpr double kMaxExactInteger = 9007199254740992.0; // 2^53
            const double raw = value.toDouble();
            if (raw < 0.0 || raw > kMaxExactInteger)
            {
                qWarning() << "[protocol] uid sent as a number out of double exact range:" << raw
                    << ", server should send it as a string";
            }
            return static_cast<std::uint64_t>(raw);
        }

        qWarning() << "[protocol] uid field is missing or of unexpected type";
        return std::nullopt;
    }
} // namespace mychat::protocol
