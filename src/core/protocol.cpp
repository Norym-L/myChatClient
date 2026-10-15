#include "protocol.h"

#include <QDebug>
#include <QJsonArray>
#include <QJsonObject>
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

    QList<UserInfo> parseSearchUserRsp(const QJsonObject& object)
    {
        QList<UserInfo> list;

        const int error = object.value(QStringLiteral("error"))
                                .toInt(static_cast<int>(ErrorCode::Success));
        if (error != static_cast<int>(ErrorCode::Success))
        {
            qDebug() << "[protocol] 搜索用户失败, error =" << error;
            return list; // 未找到（1014）或其它错误 → 空列表
        }

        // 命中时服务端返回 infos 数组，可能多条（按用户名搜索时同名用户都会带上）。
        // 兼容旧版单结果回包：没有 infos 字段时回退到顶层 uid/user_name。
        const QJsonArray items = object.value(QStringLiteral("infos")).toArray();
        if (!items.isEmpty())
        {
            list.reserve(items.size());
            for (const QJsonValue& item: items)
            {
                if (!item.isObject())
                    continue;

                const QJsonObject entry = item.toObject();
                const auto uid = parseUid(entry.value(QStringLiteral("uid")));
                if (!uid.has_value() || *uid == 0)
                    continue;

                UserInfo info;
                info.uid = *uid;
                info.name = entry.value(QStringLiteral("user_name")).toString();
                info.desc = entry.value(QStringLiteral("desc")).toString();
                info.sex = entry.value(QStringLiteral("sex")).toInt(2);
                info.icon = entry.value(QStringLiteral("icon")).toString();
                list.append(info);
            }
            return list;
        }

        // 旧版服务端回包形态：{"error":0, "uid":"...", "user_name":...}
        const auto uid = parseUid(object.value(QStringLiteral("uid")));
        if (!uid.has_value() || *uid == 0)
            return list;

        UserInfo info;
        info.uid = *uid;
        info.name = object.value(QStringLiteral("user_name")).toString();
        info.desc = object.value(QStringLiteral("desc")).toString();
        info.sex = object.value(QStringLiteral("sex")).toInt(2);
        info.icon = object.value(QStringLiteral("icon")).toString();
        list.append(info);
        return list;
    }

    QList<FriendRequest> parseFriendRequestList(const QJsonObject& object)
    {
        QList<FriendRequest> list;

        const int error = object.value(QStringLiteral("error"))
                                .toInt(static_cast<int>(ErrorCode::Success));
        if (error != static_cast<int>(ErrorCode::Success))
        {
            qDebug() << "[protocol] 获取好友申请列表失败, error =" << error;
            return list;
        }

        // 注意 count 是**字符串**（服务端用 std::to_string 写的）
        const QString count_text = object.value(QStringLiteral("count")).toString();
        qDebug() << "[protocol] 好友申请条数:" << count_text;

        const QJsonArray items = object.value(QStringLiteral("apply_infos")).toArray();
        list.reserve(items.size());

        for (const QJsonValue& item: items)
        {
            if (!item.isObject())
                continue;

            const QJsonObject entry = item.toObject();
            FriendRequest request;

            const auto id = parseUid(entry.value(QStringLiteral("id")));
            request.id = id.value_or(0);

            const auto from = parseUid(entry.value(QStringLiteral("from_uid")));
            request.fromUid = from.value_or(0);

            const auto to = parseUid(entry.value(QStringLiteral("to_uid")));
            request.toUid = to.value_or(0);

            request.name = entry.value(QStringLiteral("name")).toString();
            request.verifyInfo = entry.value(QStringLiteral("verify_info")).toString();
            request.icon = entry.value(QStringLiteral("icon")).toString();
            request.sex = entry.value(QStringLiteral("sex")).toInt(2);
            request.state = entry.value(QStringLiteral("state")).toInt(0);

            if (request.valid())
                list.append(request);
        }

        return list;
    }
} // namespace mychat::protocol
