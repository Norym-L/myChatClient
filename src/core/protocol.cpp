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
            // 服务端的注释就是"非法用户id"（const.h:1011），登录 / 搜索 / 加好友都用它
            return QObject::tr("用户不存在（uid 无效）");
        case ErrorCode::UserTokenInvalid:
            return QObject::tr("登录凭据已失效，请重新登录");
        case ErrorCode::ServiceUnavailable:
            return QObject::tr("服务器暂时不可用，请稍后重试");
        case ErrorCode::UserNotFound:
            return QObject::tr("用户不存在");
        /*
         * 下面这三条是**加好友业务失败**，长期是"够不到的代码" ——
         * 服务端 `MySqlDao::sendFriendRequest` 返回类型曾是 uint64，
         * 导致 LogicSystem 里 `if (code < 0)` 恒为假，-3/-4/-5 全被当成成功。
         * 服务端修好之后它们才真正会下发，客户端必须给得出人话。
         */
        case ErrorCode::CanNotAddYourselfAsFriend:
            return QObject::tr("不能添加自己为好友");
        case ErrorCode::UserIsYourFriendBefore:
            return QObject::tr("你们已经是好友了");
        case ErrorCode::CheckYourReceivedFriendRequest:
            return QObject::tr("对方已经给你发过好友申请，去「好友申请」里处理吧");
        case ErrorCode::UserOutline:
            return QObject::tr("对方当前不在线");
        case ErrorCode::RequestValueInvalid:
            return QObject::tr("请求参数不合法");
        case ErrorCode::DoubleHandleRequest:
            return QObject::tr("这条好友申请已经处理过了");
        case ErrorCode::JsonError:
            return QObject::tr("服务器返回数据解析失败");
        case ErrorCode::NetworkError:
            return QObject::tr("网络连接异常，请检查网络后重试");
        case ErrorCode::HttpStatusError:
            return QObject::tr("服务器接口异常，请稍后重试");
        case ErrorCode::RequestTooLong:
            return QObject::tr("提交的数据过大，请缩短后重试");
        case ErrorCode::RequestTimeout:
            return QObject::tr("网络连接超时，请稍后重试");
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
            // nick = 备注，服务端只给"我发出的那条"填值（申请人自己起的名字）
            request.nick = entry.value(QStringLiteral("nick")).toString();
            request.verifyInfo = entry.value(QStringLiteral("verify_info")).toString();
            request.icon = entry.value(QStringLiteral("icon")).toString();
            request.sex = entry.value(QStringLiteral("sex")).toInt(2);
            request.state = entry.value(QStringLiteral("state")).toInt(0);

            if (request.valid())
                list.append(request);
        }

        return list;
    }

    QList<FriendInfo> parseFriendList(const QJsonObject& object)
    {
        QList<FriendInfo> list;

        const int error = object.value(QStringLiteral("error"))
                                .toInt(static_cast<int>(ErrorCode::Success));
        if (error != static_cast<int>(ErrorCode::Success))
        {
            qDebug() << "[protocol] 获取好友列表失败, error =" << error;
            return list;
        }

        /*
         * `count` 服务端是 std::to_string 出来的字符串（文档 §8.2 写成 number 是错的），
         * 这里只当日志用 —— 真正相信的是 friends 数组的长度，
         * 因为资料查不到的好友服务端会跳过，count 与实际条数可能对不上。
         */
        const QString count_text = object.value(QStringLiteral("count")).toString();
        qDebug() << "[protocol] 好友列表条数(count 字段):" << count_text;

        // 没有好友时服务端回 "friends": null —— toArray() 对 null 返回空数组，正好走空态
        const QJsonArray items = object.value(QStringLiteral("friends")).toArray();
        list.reserve(items.size());

        for (const QJsonValue& item: items)
        {
            if (!item.isObject())
                continue;

            const QJsonObject entry = item.toObject();
            const auto uid = parseUid(entry.value(QStringLiteral("uid")));
            if (!uid.has_value() || *uid == 0)
                continue;

            FriendInfo info;
            info.uid = *uid;
            info.name = entry.value(QStringLiteral("name")).toString();
            info.desc = entry.value(QStringLiteral("desc")).toString();
            // nick = 我给对方设置的备注；没设置时服务端给空串
            info.nick = entry.value(QStringLiteral("nick")).toString();
            info.sex = entry.value(QStringLiteral("sex")).toInt(2);
            info.icon = entry.value(QStringLiteral("icon")).toString();
            list.append(info);
        }

        return list;
    }

    std::optional<bool> parseUserOnlineRsp(const QJsonObject& object)
    {
        const int error = object.value(QStringLiteral("error"))
                                .toInt(static_cast<int>(ErrorCode::JsonError));
        if (error != static_cast<int>(ErrorCode::Success))
        {
            qWarning() << "[protocol] 查询在线状态失败, error =" << error;
            return std::nullopt;
        }

        // 服务端回的是字符串 "online" / "offline"，不是布尔 —— 别用 toBool()，
        // 非空字符串会被 QJsonValue::toBool() 判成 true，"offline" 也会变在线。
        const QString online = object.value(QStringLiteral("online")).toString();
        if (online == QLatin1String("online"))
            return true;
        if (online == QLatin1String("offline"))
            return false;

        qWarning() << "[protocol] 在线状态回包里的 online 取值无法识别:" << online;
        return std::nullopt;
    }

    QList<ChatMessage> parseChatMessages(const QJsonObject& object)
    {
        QList<ChatMessage> list;

        const int error = object.value(QStringLiteral("error"))
                                .toInt(static_cast<int>(ErrorCode::JsonError));
        if (error != static_cast<int>(ErrorCode::Success))
        {
            qWarning() << "[protocol] 拉取消息失败, error =" << error;
            return list;
        }

        /*
         * ⚠ `count` 是**字符串**（服务端 `std::to_string(messages.size())`），
         *   与 1021 的 count 一致；文档 §10.1 表格里写成 number 是错的。
         *   这里只当日志：真正相信的是 msgs 数组长度（群聊消息会被服务端跳过，
         *   count 可能大于实际条数）。
         */
        qDebug() << "[protocol] 拉取回包 count 字段:" << object.value(QStringLiteral("count"));
        // 没有消息时服务端**不写** msgs 字段（不是空数组）→ toArray() 得到空数组，正好走空态
        const QJsonArray items = object.value(QStringLiteral("msgs")).toArray();
        list.reserve(items.size());

        for (const QJsonValue& item: items)
        {
            if (!item.isObject())
                continue;

            const QJsonObject entry = item.toObject();
            ChatMessage message;
            message.id = entry.value(QStringLiteral("id")).toString();
            message.fromUid = parseUid(entry.value(QStringLiteral("from_uid"))).value_or(0);
            message.body = entry.value(QStringLiteral("body")).toString();

            /*
             * `type` 文档写的是 number，但服务端的历史习惯是把数字 to_string 之后再发
             * （1013 请求里就是字符串）。两种都吃掉，别因为它换了形态就丢掉整条消息。
             */
            const QJsonValue type_value = entry.value(QStringLiteral("type"));
            message.type = type_value.isString()
                               ? type_value.toString().toInt()
                               : type_value.toInt();

            if (!message.valid())
            {
                // 缺 id（没法 ACK）或缺 from_uid（不知道是谁发的）：留着也没用，
                // 而且 ACK 不了会让它在服务端 PEL 里躺着 —— 至少要留一条日志。
                qWarning() << "[protocol] 拉取到的消息字段不完整，已跳过:" << entry;
                continue;
            }

            list.append(message);
        }

        return list;
    }
} // namespace mychat::protocol
