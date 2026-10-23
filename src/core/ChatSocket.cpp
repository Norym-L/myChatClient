#include "ChatSocket.h"

#include <QDebug>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTcpSocket>
#include <QTimer>

#include <algorithm>

#include "protocol.h"

namespace mychat
{
    namespace
    {
        /**
         * 好友备注的最大长度：与协议层的常量一致（界面输入框也用它）。
         */
        constexpr int kMaxRemarkLength = protocol::MAX_FRIEND_REMARK_LEN;

        /**
         * 从回包 body 里取客户端请求编号 `msgid`。
         *
         * 服务端原样回显（字符串就回字符串、数字就回数字），所以这里不能用
         * `toString()` 了事 —— 对数字它返回空串。缺失 / 非法 body 返回空串，
         * 调用方据此退回"同型号 FIFO"的老认领方式。
         */
        QString msgIdOf(const QByteArray& body)
        {
            if (body.isEmpty())
                return {};

            QJsonParseError error{};
            const QJsonDocument document = QJsonDocument::fromJson(body, &error);
            if (error.error != QJsonParseError::NoError || !document.isObject())
                return {};

            const QJsonValue value = document.object().value(QStringLiteral("msgid"));
            return value.isString() ? value.toString() : value.toVariant().toString();
        }

        /**
         * 从群管理回包（1034 / 1036 / 1038 / 1040 / 1042）里取 `error`。
         *
         * @return std::nullopt 表示**回包里没有 error 字段**。
         *
         * 服务端有几处 Redis 异常会直接逃逸出处理器（`api-group.md` §4.2 / §5.2 /
         * §7.3），那种回包只剩一个 `msgid`。它既不是成功也不能当 0 ——
         * 必须按失败上报，否则界面会把一次没发生的事显示成做成了。
         */
        std::optional<int> replyErrorCode(const QByteArray& body)
        {
            QJsonParseError parse_error{};
            const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
            if (parse_error.error != QJsonParseError::NoError || !document.isObject())
                return std::nullopt;

            const QJsonValue value = document.object().value(QStringLiteral("error"));
            // error 服务端一律写成数字；字符串形态只是兼容，别对 null 调 toInt（会得 0）
            if (value.isDouble())
                return value.toInt();
            if (value.isString())
                return value.toString().toInt();
            return std::nullopt;
        }

        /// 从回包 body 里取一个字符串字段（缺失 / 解析失败返回空串）
        QString stringField(const QByteArray& body, const QString& key)
        {
            QJsonParseError parse_error{};
            const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
            if (parse_error.error != QJsonParseError::NoError || !document.isObject())
                return {};

            const QJsonValue value = document.object().value(key);
            return value.isString() ? value.toString() : value.toVariant().toString();
        }

        /**
         * 这串东西是不是一个纯数字的 uint64 标识。
         *
         * 服务端所有 uid / group_id / gid 都按 uint64 解析（`uid_codec::ParseUid`
         * → `std::from_chars` 整串匹配），一个非数字字符就会让整条请求变成 1020。
         *
         * 客户端里唯一会混进来的非数字标识是**群会话键** `g<gid>`
         * （`ChatController::groupKey`）—— 那是内存与 SQLite 里的会话主键，
         * **永远不该出现在网络层**。群号本身是 uint64（`groups.id`）。
         * 所有出网前的校验都走这一个判断，别各写各的。
         */
        bool isNumericId(const QString& text)
        {
            bool be_valid = false;
            text.trimmed().toULongLong(&be_valid);
            return be_valid;
        }

        /// message_id -> 名称，仅用于日志
        const char* messageIdName(const std::uint16_t id)
        {
            switch (static_cast<protocol::MessageId>(id))
            {
            case protocol::MessageId::ChatLoginReq:
                return "ChatLoginReq";
            case protocol::MessageId::ChatLoginRsp:
                return "ChatLoginRsp";
            case protocol::MessageId::SearchUserReq:
                return "SearchUserReq";
            case protocol::MessageId::SearchUserRsp:
                return "SearchUserRsp";
            case protocol::MessageId::AddFriendReq:
                return "AddFriendReq";
            case protocol::MessageId::AddFriendRsp:
                return "AddFriendRsp";
            case protocol::MessageId::SyncFriendRequestReq:
                return "SyncFriendRequestReq";
            case protocol::MessageId::SyncFriendRequestRsp:
                return "SyncFriendRequestRsp";
            case protocol::MessageId::NotifyAddFriendReq:
                return "NotifyAddFriendReq";
            case protocol::MessageId::AuthFriendReq:
                return "AuthFriendReq";
            case protocol::MessageId::AuthFriendRsp:
                return "AuthFriendRsp";
            case protocol::MessageId::NotifyAuthFriendReq:
                return "NotifyAuthFriendReq";
            case protocol::MessageId::TextChatMsgReq:
                return "TextChatMsgReq";
            case protocol::MessageId::TextChatMsgRsp:
                return "TextChatMsgRsp";
            case protocol::MessageId::NotifyTextChatMsgReq:
                return "NotifyTextChatMsgReq";
            case protocol::MessageId::HeartBeatReq:
                return "HeartBeatReq";
            case protocol::MessageId::HeartBeatRsp:
                return "HeartBeatRsp";
            case protocol::MessageId::CheckIsUserOnlineReq:
                return "CheckIsUserOnlineReq";
            case protocol::MessageId::CheckIsUserOnlineRsp:
                return "CheckIsUserOnlineRsp";
            case protocol::MessageId::SyncFriendListReq:
                return "SyncFriendListReq";
            case protocol::MessageId::SyncFriendListRsp:
                return "SyncFriendListRsp";
            case protocol::MessageId::NotifyReceiveMessage:
                return "NotifyReceiveMessage";
            case protocol::MessageId::SyncChatMessageReq:
                return "SyncChatMessageReq";
            case protocol::MessageId::SyncChatMessageRsp:
                return "SyncChatMessageRsp";
            case protocol::MessageId::AckChatMessageReq:
                return "AckChatMessageReq";
            case protocol::MessageId::AckChatMessageRsp:
                return "AckChatMessageRsp";
            case protocol::MessageId::DelFriendReq:
                return "DelFriendReq";
            case protocol::MessageId::DelFriendRsp:
                return "DelFriendRsp";
            case protocol::MessageId::SetFriendRemarkReq:
                return "SetFriendRemarkReq";
            case protocol::MessageId::SetFriendRemarkRsp:
                return "SetFriendRemarkRsp";
            case protocol::MessageId::ChangeUserInfoReq:
                return "ChangeUserInfoReq";
            case protocol::MessageId::ChangeUserInfoRsp:
                return "ChangeUserInfoRsp";
            case protocol::MessageId::GroupCreateReq:
                return "GroupCreateReq";
            case protocol::MessageId::GroupCreateRsp:
                return "GroupCreateRsp";
            case protocol::MessageId::GroupAddMembersReq:
                return "GroupAddMembersReq";
            case protocol::MessageId::GroupAddMembersRsp:
                return "GroupAddMembersRsp";
            case protocol::MessageId::GroupKickMemberReq:
                return "GroupKickMemberReq";
            case protocol::MessageId::GroupKickMemberRsp:
                return "GroupKickMemberRsp";
            case protocol::MessageId::GroupDismissReq:
                return "GroupDismissReq";
            case protocol::MessageId::GroupDismissRsp:
                return "GroupDismissRsp";
            case protocol::MessageId::GetGroupUserInfoReq:
                return "GetGroupUserInfoReq";
            case protocol::MessageId::GetGroupUserInfoRsp:
                return "GetGroupUserInfoRsp";
            case protocol::MessageId::GetGroupInfoReq:
                return "GetGroupInfoReq";
            case protocol::MessageId::GetGroupInfoRsp:
                return "GetGroupInfoRsp";
            case protocol::MessageId::GroupInfoChangeReq:
                return "GroupInfoChangeReq";
            case protocol::MessageId::GroupInfoChangeRsp:
                return "GroupInfoChangeRsp";
            }
            return "Unknown";
        }
    } // namespace

    ChatSocket& ChatSocket::instance()
    {
        static ChatSocket socket;
        return socket;
    }

    ChatSocket::ChatSocket(QObject* parent)
        : QObject(parent)
          , m_socket(new QTcpSocket(this))
          , m_connect_timer(new QTimer(this))
          , m_heartbeat_timer(new QTimer(this))
        , m_reply_timer(new QTimer(this))
    {
        m_clock.start();

        m_connect_timer->setSingleShot(true);
        connect(m_connect_timer, &QTimer::timeout, this, [this]()
        {
            qWarning() << "[chat] 连接超时:" << m_host << m_port;
            reset();
            emit connectFailed(tr("连接聊天服务器超时"));
        });

        /*
         * 心跳：登录成功后开始跳，断开就停。
         *
         * 不加"先等一拍"的措辞 —— 登录本身已经把在线 key 写进去了
         * （服务端 loginHandler 里 set(USER_IP_PERFIX + uid)），所以不必登录后立刻补一发。
         */
        m_heartbeat_timer->setInterval(protocol::HEARTBEAT_INTERVAL_MS);
        connect(m_heartbeat_timer, &QTimer::timeout, this, [this]()
                    {
                        sendHeartbeat();
                    });

        /*
         * 回包超时的统一出口。
         *
         * 之前是每条链路各配一只定时器（查在线一只、处理申请一只），漏一条就是
         * "界面永远转圈 + 后续请求被上一个挡住"。现在所有请求共用一张表、一只表，
         * 加新消息只要发的时候调一次 expectReply() —— 回包那边 handleFrame 会自己认领。
         */
        m_reply_timer->setSingleShot(true);
        connect(m_reply_timer, &QTimer::timeout, this, [this]()
                    {
                        if (m_pending.isEmpty())
                            return;

                        // 只处理到期的那一条；处理完重新起表，下一条由下一次 timeout 带走
                        PendingReply entry = m_pending.takeFirst();

                        /*
                         * 服务端保证「处理了就一定带 msgid 回包」，所以这里超时
                         * 等于「这条请求被丢了」—— 把编号一并报出去，调用方才能
                         * 判断该重试还是该提示（丢包时服务端不会回错误码）。
                         */
                        qWarning() << "[chat] 等待回包超时:" << messageIdName(entry.reply_id)
                            << "(请求" << messageIdName(entry.request_id)
                            << ", msgid =" << entry.msgid << ")";

                        emit requestTimedOut(entry.msgid, entry.request_id);
                        entry.on_timeout();

                        armReplyTimer();
                    });

        connect(m_socket, &QTcpSocket::connected, this, &ChatSocket::onConnected);
        connect(m_socket, &QTcpSocket::disconnected, this, &ChatSocket::onDisconnected);
        connect(m_socket, &QTcpSocket::readyRead, this, &ChatSocket::onReadyRead);
        connect(m_socket, &QAbstractSocket::errorOccurred, this, &ChatSocket::onSocketError);
    }

    bool ChatSocket::isConnected() const
    {
        return m_socket->state() == QAbstractSocket::ConnectedState;
    }

    // ── 回包超时的一致性封装 ────────────────────────────────────────────

    void ChatSocket::expectReply(const std::uint16_t requestId, const std::uint16_t replyId,
                                 const QString& msgid, std::function<void()> onTimeout)
    {
        m_pending.append(PendingReply{
            msgid, requestId, replyId, m_clock.elapsed() + kReplyTimeoutMs, std::move(onTimeout)
        });
        armReplyTimer();
    }

    QString ChatSocket::nextMsgId()
    {
        /*
         * 用 `r-`（request）而不是服务端文档示例里的 `c-`：
         * 聊天消息的业务编号 `msg_id` 已经是 `c-<本地气泡 id>`（见 ChatController），
         * 两套编号撞同一个前缀时，日志里 `msg_id: c-5` 与 `msgid: c-5` 会分不清。
         */
        return QStringLiteral("r-") + QString::number(++m_msg_seq);
    }

    void ChatSocket::finishReply(const std::uint16_t replyId, const QString& msgid)
    {
        /*
         * 优先按 msgid 精确认领：这样即使同类请求并发在飞（例如连发几条心跳、
         * 多个「查在线」），也不会出现"先回的那条把别人的账销掉"。
         */
        if (!msgid.isEmpty())
        {
            for (int i = 0; i < m_pending.size(); ++i)
            {
                if (m_pending.at(i).reply_id != replyId || m_pending.at(i).msgid != msgid)
                    continue;

                m_pending.removeAt(i);
                armReplyTimer();
                return;
            }
        }

        // 回包没带编号（主动推送 / 老服务端）：退回同型号 FIFO，保持旧行为
        for (int i = 0; i < m_pending.size(); ++i)
        {
            if (m_pending.at(i).reply_id != replyId)
                continue;

            m_pending.removeAt(i);
            armReplyTimer();
            return;
        }
    }

    void ChatSocket::armReplyTimer()
    {
        if (m_pending.isEmpty())
        {
            m_reply_timer->stop();
            return;
        }

        qint64 soonest = m_pending.first().deadline_ms;
        for (const PendingReply& entry: m_pending)
            soonest = std::min(soonest, entry.deadline_ms);

        const int remain = static_cast<int>(soonest - m_clock.elapsed());
        m_reply_timer->start(std::max(0, remain));
    }

    void ChatSocket::clearPendingReplies()
    {
        // 不给回调：连接都没了，再补一句"超时"只会盖掉真正的断线提示
        m_pending.clear();
        m_reply_timer->stop();
    }

    void ChatSocket::connectToServer(const QString& host, const std::uint16_t port)
    {
        if (host.isEmpty() || port == 0)
        {
            qWarning() << "[chat] ChatServer 地址非法:" << host << port;
            emit connectFailed(tr("聊天服务器地址无效"));
            return;
        }

        reset();

        m_host = host;
        m_port = port;

        qDebug() << "[chat] 正在连接" << m_host << m_port;
        m_connect_timer->start(kConnectTimeoutMs);
        m_socket->connectToHost(m_host, m_port);
    }

    bool ChatSocket::login(const std::uint64_t uid, const QString& token)
    {
        // 约定：uid 以 uint64 的**字符串**形式传递（QJsonValue 内部是 double，>2^53 会丢精度）
        QJsonObject body;
        body.insert(QStringLiteral("uid"), QString::number(uid));
        body.insert(QStringLiteral("token"), token);

        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::ChatLoginReq), body);
        if (msgid.isEmpty())
            return false;

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::ChatLoginReq),
                    static_cast<std::uint16_t>(protocol::MessageId::ChatLoginRsp), msgid,
                    [this]()
                        {
                            emit loginFailed(static_cast<int>(protocol::ErrorCode::RequestTimeout));
                        });
        return true;
    }

bool ChatSocket::searchUser(const QString& keyword)
{
    if (keyword.trimmed().isEmpty())
    {
        qWarning() << "[chat] 搜索关键字为空，请求被丢弃";
        return false;
    }

    // 服务端先按 uid 查、再按用户名查，所以关键字原样作为字符串发过去
    QJsonObject body;
    body.insert(QStringLiteral("uid"), keyword.trimmed());

    const QString msgid = sendJsonWithId(
        static_cast<std::uint16_t>(protocol::MessageId::SearchUserReq), body);
    if (msgid.isEmpty())
        return false;

    expectReply(static_cast<std::uint16_t>(protocol::MessageId::SearchUserReq),
                static_cast<std::uint16_t>(protocol::MessageId::SearchUserRsp), msgid,
                [this]()
                    {
                        emit searchUserResult(false, {},
                                              static_cast<int>(protocol::ErrorCode::RequestTimeout));
                    });
    return true;
}

bool ChatSocket::sendFriendRequest(const std::uint64_t selfUid, const std::uint64_t toUid,
                                   const QString& verifyInfo, const QString& remark)
{
    QJsonObject body;
    body.insert(QStringLiteral("uid"), QString::number(selfUid));
    body.insert(QStringLiteral("to_uid"), QString::number(toUid));
    body.insert(QStringLiteral("verify_info"), verifyInfo);
    body.insert(QStringLiteral("remark"), remark);

    const QString msgid = sendJsonWithId(
        static_cast<std::uint16_t>(protocol::MessageId::AddFriendReq), body);
    if (msgid.isEmpty())
        return false;

    expectReply(static_cast<std::uint16_t>(protocol::MessageId::AddFriendReq),
                static_cast<std::uint16_t>(protocol::MessageId::AddFriendRsp), msgid,
                [this]()
                    {
                        emit friendRequestResult(false,
                                                 static_cast<int>(protocol::ErrorCode::RequestTimeout), 0);
                    });
    return true;
}

    bool ChatSocket::syncFriendRequests(const std::uint64_t selfUid)
    {
        QJsonObject body;
        body.insert(QStringLiteral("uid"), QString::number(selfUid));

        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::SyncFriendRequestReq), body);
        if (msgid.isEmpty())
            return false;

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::SyncFriendRequestReq),
                    static_cast<std::uint16_t>(protocol::MessageId::SyncFriendRequestRsp), msgid,
                    [this]()
                        {
                            emit friendRequestList(
                                {}, static_cast<int>(protocol::ErrorCode::RequestTimeout));
                        });
        return true;
    }

    bool ChatSocket::syncFriendList()
    {
        /*
         * 1020 的请求体服务端**完全不看**（syncUserFriendList 只读 session->getUserId()），
         * 所以发空对象。仍然要发 "{}" 而不是 0 长度 body —— 理由同心跳：
         * body_len == 0 会让服务端走那条没被验证过的零长读取分支。
         */
        const QJsonObject body;

        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::SyncFriendListReq), body);
        if (msgid.isEmpty())
            return false;

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::SyncFriendListReq),
                    static_cast<std::uint16_t>(protocol::MessageId::SyncFriendListRsp), msgid,
                    [this]()
                        {
                            emit friendListResult(
                                {}, static_cast<int>(protocol::ErrorCode::RequestTimeout));
                        });
        return true;
    }

    bool ChatSocket::delFriend(const QString& uid)
    {
        const QString target = uid.trimmed();

        if (!isConnected())
            return false;

        if (target.isEmpty())
        {
            qWarning() << "[chat] 删除好友的 uid 为空，请求被丢弃";
            return false;
        }

        if (!m_del_friend_in_flight.isEmpty())
        {
            /*
             * 现在回包已能按 msgid 精确认领（1028 也带），并发技术上可行；
             * 但界面一次只能点一个删除按钮，保留串行可以少一类状态交错。
             */
            qWarning() << "[chat] 上一个删除好友请求还没回包，本次丢弃:" << target;
            return false;
        }

        QJsonObject body;
        body.insert(QStringLiteral("uid"), target);

        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::DelFriendReq), body);
        if (msgid.isEmpty())
            return false;

        m_del_friend_in_flight = target;
        expectReply(static_cast<std::uint16_t>(protocol::MessageId::DelFriendReq),
                    static_cast<std::uint16_t>(protocol::MessageId::DelFriendRsp), msgid,
                    [this, target]()
                        {
                            m_del_friend_in_flight.clear();
                            emit friendDeleteResult(
                                false, static_cast<int>(protocol::ErrorCode::RequestTimeout),
                                target);
                        });
        return true;
    }

    bool ChatSocket::setFriendRemark(const QString& friendUid, const QString& remark)
    {
        const QString target = friendUid.trimmed();

        if (!isConnected())
            return false;

        if (target.isEmpty())
        {
            qWarning() << "[chat] 设置备注的好友 uid 为空，请求被丢弃";
            return false;
        }

        /*
         * 备注长度：服务端文档没约定上限（字段长度由 `user_friend.remark` 决定），
         * 超长会被 MySQL 截断或直接报 1013 —— 那时界面只会说"数据库不可用"，
         * 完全指不到真正的原因。这里先卡在 64 个字符（和界面输入框一致）。
         */
        if (remark.size() > kMaxRemarkLength)
        {
            qWarning() << "[chat] 好友备注过长，请求被丢弃, 长度 =" << remark.size();
            return false;
        }

        if (!m_remark_uid_in_flight.isEmpty())
        {
            // 回包已能按 msgid 认领；界面同一时刻只能改一个备注，保留串行减少交错
            qWarning() << "[chat] 上一个备注请求还没回包，本次丢弃:" << target;
            return false;
        }

        QJsonObject body;
        /*
         * 2026-10-22 起 1029 不再有自己的 `id`：统一用 msgid 回显（服务端把
         * 各处理器里单独的 id 回显删掉了）。这里不用再手工塞编号。
         */
        body.insert(QStringLiteral("friend_uid"), target);
        body.insert(QStringLiteral("remark"), remark);

        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::SetFriendRemarkReq), body);
        if (msgid.isEmpty())
            return false;

        m_remark_uid_in_flight = target;
        m_remark_value_in_flight = remark;
        expectReply(static_cast<std::uint16_t>(protocol::MessageId::SetFriendRemarkReq),
                    static_cast<std::uint16_t>(protocol::MessageId::SetFriendRemarkRsp), msgid,
                    [this]()
                        {
                            const QString uid = m_remark_uid_in_flight;
                            const QString value = m_remark_value_in_flight;
                            m_remark_uid_in_flight.clear();
                            m_remark_value_in_flight.clear();
                            emit friendRemarkResult(
                                false, static_cast<int>(protocol::ErrorCode::RequestTimeout),
                                uid, value);
                        });
        return true;
    }

    bool ChatSocket::changeUserInfo(const QString& name, const QString& desc, int sex)
    {
        const QString new_name = name.trimmed();

        if (!isConnected())
            return false;

        if (new_name.isEmpty())
        {
            qWarning() << "[chat] 昵称为空，编辑资料请求被丢弃";
            return false;
        }

        if (m_profile_in_flight)
        {
            qWarning() << "[chat] 上一个编辑资料请求还没回包，本次丢弃";
            return false;
        }

        // 服务端的 Sex 枚举是 0 男 / 1 女 / 2 未知，其它值一律按未知发
        const int safe_sex = (sex == 0 || sex == 1) ? sex : 2;

        QJsonObject body;
        // 2026-10-22 起 1031 也不需要自己的 `id`：统一由 sendJsonWithId 注入 msgid
        body.insert(QStringLiteral("name"), new_name);
        body.insert(QStringLiteral("desc"), desc);
        body.insert(QStringLiteral("sex"), safe_sex);

        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::ChangeUserInfoReq), body);
        if (msgid.isEmpty())
            return false;

        m_profile_in_flight = true;
        m_profile_name_in_flight = new_name;
        m_profile_desc_in_flight = desc;
        m_profile_sex_in_flight = safe_sex;
        expectReply(static_cast<std::uint16_t>(protocol::MessageId::ChangeUserInfoReq),
                    static_cast<std::uint16_t>(protocol::MessageId::ChangeUserInfoRsp), msgid,
                    [this]()
                        {
                            const QString timeout_name = m_profile_name_in_flight;
                            const QString timeout_desc = m_profile_desc_in_flight;
                            const int timeout_sex = m_profile_sex_in_flight;
                            m_profile_in_flight = false;
                            m_profile_name_in_flight.clear();
                            m_profile_desc_in_flight.clear();
                            emit userInfoChangeResult(
                                false, static_cast<int>(protocol::ErrorCode::RequestTimeout),
                                timeout_name, timeout_desc, timeout_sex);
                        });
        return true;
    }

    bool ChatSocket::sendTextMessage(const QString& msgId, const QString& toUid, const QString& body,
                                     const protocol::ChatMessageType type, const bool group)
    {
        const QString target = toUid.trimmed();
        const QString id = msgId.trimmed();

        /*
         * msgId 是回执唯一的配对依据（服务端只回显、不生成）：
         * 传空的话回包就认不回来了，宁可在这里拒发。
         */
        if (id.isEmpty())
        {
            qWarning() << "[chat] msg_id 为空，消息被丢弃（回执将无法认领）";
            return false;
        }

        // 服务端的 to_uid 解析失败 / type 越界都会回 1020，本地先挡一层，省一次往返。
        // 这类错是客户端自己写错，不值得让服务端来告诉用户。
        if (target.isEmpty() || body.isEmpty())
        {
            qWarning() << "[chat] 消息参数不合法，请求被丢弃: to_uid =" << toUid
                << "body 长度 =" << body.size();
            return false;
        }

        const auto type_value = static_cast<int>(type);
        if (type_value < 1 || type_value > 4)
        {
            qWarning() << "[chat] 非法的消息类型:" << type_value;
            return false;
        }

        /*
         * 未登录就别发了：服务端 `sendTextMessageHandler` **不校验登录态**，
         * 照样会以 from_uid = 0 写进对方的离线流（api-chat.md §8.2 第 2 条）。
         * 那不是"能发"，那是往别人的消息列表里塞一条无主消息。
         *
         * ⚠ 群消息更糟：服务端会 `SISMEMBER chatgroup_<gid> 0`，校验不过直接
         *   回 1022；但这一条在界面上会表现成"发送失败：你们还不是好友"，
         *   完全看不出真实原因。所以本地先挡。
         */
        if (!isConnected() || !m_logged_in)
        {
            qWarning() << "[chat] 未登录或未连接，消息无法发送:" << target << (group ? "(群)" : "");
            return false;
        }

        m_text_queue.append(OutgoingText{id, target, body, type_value, group});
        pumpTextQueue();
        return true;
    }

    void ChatSocket::pumpTextQueue()
    {
        /*
         * 串行一发的循环：发出去的立刻 return 等回包，发不出去的继续看下一条。
         *
         * 这里用 while 而不是递归 —— 一旦连接刚断，队列里积压的每一条都会走
         * "发不出去"分支，递归会变成"队列长度 = 栈深度"。
         */
        while (!m_text_in_flight.has_value() && !m_text_queue.isEmpty())
        {
            const OutgoingText message = m_text_queue.takeFirst();

            // 四个字段服务端都按**字符串**读（api-chat.md §8.1）；type 给数字虽然也能被
            // asString() 兼容，但没必要赌它哪天换成严格校验。
            QJsonObject body;
            body.insert(QStringLiteral("msg_id"), message.msg_id);
            body.insert(QStringLiteral("to_uid"), message.to_uid);
            body.insert(QStringLiteral("type"), QString::number(message.type));
            body.insert(QStringLiteral("body"), message.body);
            /*
             * 群消息标记（2026-10-23）。只有 true 时才写这个字段：
             *   · 服务端 `root["group"].asBool()`，缺省就是 false，单聊不必多发；
             *   · 必须发 **JSON 布尔** —— `asBool()` 对非空字符串也返回真，
             *     以后若有人图省事发 "false"，单聊会被当成群消息发出去。
             */
            if (message.group)
                body.insert(QStringLiteral("group"), true);

            const QString msgid = sendJsonWithId(
                static_cast<std::uint16_t>(protocol::MessageId::TextChatMsgReq), body);
            if (msgid.isEmpty())
            {
                // 连接断了 / body 超过 2048：立刻按失败上报，别让队列装作还在发
                emit textMessageResult(false,
                                       static_cast<int>(protocol::ErrorCode::NetworkError),
                                       message.msg_id, message.to_uid, message.body);
                continue;
            }

            m_text_in_flight = message;

            expectReply(static_cast<std::uint16_t>(protocol::MessageId::TextChatMsgReq),
                        static_cast<std::uint16_t>(protocol::MessageId::TextChatMsgRsp), msgid,
                        [this]()
                            {
                                // 服务端一句都没回：这条到底写没写进去，谁都说不清。
                                // 按失败报（界面标红），并把队列放行 —— 否则后面全卡死。
                                const OutgoingText pending = *m_text_in_flight;
                                m_text_in_flight.reset();

                                emit textMessageResult(
                                    false, static_cast<int>(protocol::ErrorCode::RequestTimeout),
                                    pending.msg_id, pending.to_uid, pending.body);

                                pumpTextQueue();
                            });
            return;
        }
    }

    bool ChatSocket::syncChatMessages(const int count)
    {
        if (!isConnected() || !m_logged_in)
        {
            // 服务端不校验登录态，未登录会以 uid=0 去读一个不存在的消费组 —— 本地先挡
            qWarning() << "[chat] 未登录或未连接，无法拉取消息";
            return false;
        }

        if (m_pull_in_flight)
        {
            // 现在 1024 也带 msgid、技术上可并发；但拉取本身是"有积压才拉"，
            // 并发只会重复拿同一批，保留串行更省事。
            qDebug() << "[chat] 上一个拉取还没回包，本次跳过";
            return false;
        }

        const int n = std::clamp(count, 1, protocol::MAX_CHAT_MESSAGES_PER_BATCH);

        QJsonObject body;
        body.insert(QStringLiteral("count"), QString::number(n)); // 服务端按字符串读

        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::SyncChatMessageReq), body);
        if (msgid.isEmpty())
            return false;

        m_pull_in_flight = true;

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::SyncChatMessageReq),
                    static_cast<std::uint16_t>(protocol::MessageId::SyncChatMessageRsp), msgid,
                    [this]()
                        {
                            m_pull_in_flight = false;
                            emit chatMessagesReceived(
                                {}, static_cast<int>(protocol::ErrorCode::RequestTimeout));
                        });
        return true;
    }

    bool ChatSocket::ackChatMessages(const QStringList& ids)
    {
        if (ids.isEmpty())
            return false;

        if (ids.size() > protocol::MAX_CHAT_MESSAGES_PER_BATCH)
        {
            // 服务端 >10 直接回 1020。这里不替调用方切分 —— 切分意味着"确认了一半"，
            // 调用方应该自己按批调（一次拉 10 条，正好一批）。
            qWarning() << "[chat] 一次最多确认" << protocol::MAX_CHAT_MESSAGES_PER_BATCH
                << "条，实际" << ids.size() << "条，已拒绝";
            return false;
        }

        if (!isConnected() || !m_logged_in)
        {
            qWarning() << "[chat] 未登录或未连接，无法确认消息";
            return false;
        }

        m_ack_queue.append(ids);
        pumpAckQueue();
        return true;
    }

    void ChatSocket::pumpAckQueue()
    {
        while (!m_ack_in_flight && !m_ack_queue.isEmpty())
        {
            const QStringList ids = m_ack_queue.takeFirst();

            QJsonArray array;
            for (const QString& id: ids)
                array.append(id);

            QJsonObject body;
            body.insert(QStringLiteral("ids"), array); // 唯一的数组字段，别写成字符串

            const QString msgid = sendJsonWithId(
                static_cast<std::uint16_t>(protocol::MessageId::AckChatMessageReq), body);
            if (msgid.isEmpty())
            {
                emit chatMessagesAcked(false,
                                       static_cast<int>(protocol::ErrorCode::NetworkError));
                continue;
            }

            m_ack_in_flight = true;

            expectReply(static_cast<std::uint16_t>(protocol::MessageId::AckChatMessageReq),
                        static_cast<std::uint16_t>(protocol::MessageId::AckChatMessageRsp), msgid,
                        [this]()
                            {
                                m_ack_in_flight = false;
                                emit chatMessagesAcked(
                                    false, static_cast<int>(protocol::ErrorCode::RequestTimeout));
                                pumpAckQueue();
                            });
            return;
        }
    }

    // ══════════════════════════════════════════════════════════════════
    //  群聊管理（1033–1042）
    // ══════════════════════════════════════════════════════════════════

    bool ChatSocket::createGroup(const QString& name, const QString& avatar,
                                 const QString& announcement)
    {
        if (!isConnected() || !m_logged_in)
        {
            qWarning() << "[chat] 未登录或未连接，无法创建群聊";
            return false;
        }

        if (m_group_requests.size() >= kMaxGroupRequestsInFlight)
        {
            qWarning() << "[chat] 群请求积压过多(" << m_group_requests.size()
                << ")，本次创建群聊被丢弃";
            return false;
        }

        /*
         * 字段名就是 `avator`（服务端 `root["avator"]`）—— 拼错的是协议，
         * 不是笔误；发 `avatar` 会被静默忽略，群头像就永远不会写进库。
         */
        QJsonObject body;
        body.insert(QStringLiteral("name"), name);
        body.insert(QStringLiteral("avator"), avatar);
        body.insert(QStringLiteral("announcement"), announcement);

        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::GroupCreateReq), body);
        if (msgid.isEmpty())
            return false;

        GroupRequest request;
        request.kind = GroupRequest::Kind::Create;
        request.name = name;
        request.avatar = avatar;
        request.announcement = announcement;
        m_group_requests.insert(msgid, request);

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::GroupCreateReq),
                    static_cast<std::uint16_t>(protocol::MessageId::GroupCreateRsp), msgid,
                    [this, msgid, name]()
                        {
                            m_group_requests.remove(msgid);
                            // 群没建成，没有 group id 可回
                            emit groupCreated(false,
                                              static_cast<int>(protocol::ErrorCode::RequestTimeout),
                                              {}, name);
                        });
        return true;
    }

    bool ChatSocket::addGroupMembers(const QString& gid, const QStringList& uids)
    {
        if (!isConnected() || !m_logged_in)
        {
            qWarning() << "[chat] 未登录或未连接，无法拉人入群";
            return false;
        }

        if (!isNumericId(gid) || uids.isEmpty())
        {
            qWarning() << "[chat] 拉人参数不合法: gid =" << gid << "成员数 =" << uids.size();
            return false;
        }

        if (m_group_requests.size() >= kMaxGroupRequestsInFlight)
        {
            qWarning() << "[chat] 群请求积压过多(" << m_group_requests.size()
                << ")，本次拉人入群被丢弃";
            return false;
        }

        /*
         * `members` 必须是**数组**：服务端 `!root["members"].isArray()` 直接回 1020。
         * 元素用字符串（服务端 `member.asString()` 再解析成 uid）。
         */
        QJsonArray array;
        for (const QString& uid: uids)
            array.append(uid);

        QJsonObject body;
        body.insert(QStringLiteral("group_id"), gid);
        body.insert(QStringLiteral("members"), array);

        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::GroupAddMembersReq), body);
        if (msgid.isEmpty())
            return false;

        GroupRequest request;
        request.kind = GroupRequest::Kind::AddMembers;
        request.gid = gid;
        request.members = uids;
        m_group_requests.insert(msgid, request);

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::GroupAddMembersReq),
                    static_cast<std::uint16_t>(protocol::MessageId::GroupAddMembersRsp), msgid,
                    [this, msgid, gid, uids]()
                        {
                            m_group_requests.remove(msgid);
                            emit groupMembersAdded(
                                false, static_cast<int>(protocol::ErrorCode::RequestTimeout),
                                gid, uids);
                        });
        return true;
    }

    bool ChatSocket::kickGroupMember(const QString& gid, const QString& uid)
    {
        if (!isConnected() || !m_logged_in)
        {
            qWarning() << "[chat] 未登录或未连接，无法踢人";
            return false;
        }

        if (!isNumericId(gid) || !isNumericId(uid))
        {
            qWarning() << "[chat] 踢人参数不合法: gid =" << gid << "uid =" << uid;
            return false;
        }

        if (m_group_requests.size() >= kMaxGroupRequestsInFlight)
        {
            qWarning() << "[chat] 群请求积压过多(" << m_group_requests.size()
                << ")，本次踢人被丢弃";
            return false;
        }

        QJsonObject body;
        body.insert(QStringLiteral("group_id"), gid);
        body.insert(QStringLiteral("uid"), uid);

        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::GroupKickMemberReq), body);
        if (msgid.isEmpty())
            return false;

        GroupRequest request;
        request.kind = GroupRequest::Kind::Kick;
        request.gid = gid;
        request.target_uid = uid;
        m_group_requests.insert(msgid, request);

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::GroupKickMemberReq),
                    static_cast<std::uint16_t>(protocol::MessageId::GroupKickMemberRsp), msgid,
                    [this, msgid, gid, uid]()
                        {
                            m_group_requests.remove(msgid);
                            emit groupMemberKicked(
                                false, static_cast<int>(protocol::ErrorCode::RequestTimeout),
                                gid, uid);
                        });
        return true;
    }

    bool ChatSocket::dismissGroup(const QString& gid)
    {
        if (!isConnected() || !m_logged_in)
        {
            qWarning() << "[chat] 未登录或未连接，无法解散群聊";
            return false;
        }

        if (!isNumericId(gid))
        {
            qWarning() << "[chat] 解散群聊缺少合法的 group_id:" << gid;
            return false;
        }

        if (m_group_requests.size() >= kMaxGroupRequestsInFlight)
        {
            qWarning() << "[chat] 群请求积压过多(" << m_group_requests.size()
                << ")，本次解散被丢弃";
            return false;
        }

        QJsonObject body;
        body.insert(QStringLiteral("group_id"), gid);

        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::GroupDismissReq), body);
        if (msgid.isEmpty())
            return false;

        GroupRequest request;
        request.kind = GroupRequest::Kind::Dismiss;
        request.gid = gid;
        m_group_requests.insert(msgid, request);

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::GroupDismissReq),
                    static_cast<std::uint16_t>(protocol::MessageId::GroupDismissRsp), msgid,
                    [this, msgid, gid]()
                        {
                            m_group_requests.remove(msgid);
                            emit groupDismissed(
                                false, static_cast<int>(protocol::ErrorCode::RequestTimeout),
                                gid);
                        });
        return true;
    }

    bool ChatSocket::fetchGroupMembers(const QString& gid)
    {
        if (!isConnected() || !m_logged_in)
        {
            qWarning() << "[chat] 未登录或未连接，无法获取群成员";
            return false;
        }

        if (!isNumericId(gid))
        {
            qWarning() << "[chat] 获取群成员缺少合法的 gid:" << gid;
            return false;
        }

        if (m_group_requests.size() >= kMaxGroupRequestsInFlight)
        {
            qWarning() << "[chat] 群请求积压过多(" << m_group_requests.size()
                << ")，本次获取群成员被丢弃";
            return false;
        }

        /*
         * ⚠ 字段名是 `gid` —— 服务端 `root["gid"]`。
         *   和 1035/1037/1039 的 `group_id` 不一致，这是服务端的历史包袱，
         *   写成 group_id 会直接被判成 1020（参数非法）。
         */
        QJsonObject body;
        body.insert(QStringLiteral("gid"), gid);

        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::GetGroupUserInfoReq), body);
        if (msgid.isEmpty())
            return false;

        GroupRequest request;
        request.kind = GroupRequest::Kind::FetchMembers;
        request.gid = gid;
        m_group_requests.insert(msgid, request);

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::GetGroupUserInfoReq),
                    static_cast<std::uint16_t>(protocol::MessageId::GetGroupUserInfoRsp), msgid,
                    [this, msgid, gid]()
                        {
                            m_group_requests.remove(msgid);
                            emit groupMembersReceived(
                                false, static_cast<int>(protocol::ErrorCode::RequestTimeout),
                                gid, {});
                        });
        return true;
    }

    bool ChatSocket::getGroupInfo(const QString& gid)
    {
        if (!isConnected() || !m_logged_in)
        {
            qWarning() << "[chat] 未登录或未连接，无法获取群资料";
            return false;
        }

        if (!isNumericId(gid))
        {
            qWarning() << "[chat] 获取群资料缺少合法的 gid:" << gid;
            return false;
        }

        if (m_group_requests.size() >= kMaxGroupRequestsInFlight)
        {
            qWarning() << "[chat] 群请求积压过多(" << m_group_requests.size()
                << ")，本次获取群资料被丢弃";
            return false;
        }

        // 1043 的字段名同样是 `gid`（和 1041 一致，不是 1035/1037/1039 的 group_id）
        QJsonObject body;
        body.insert(QStringLiteral("gid"), gid);

        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::GetGroupInfoReq), body);
        if (msgid.isEmpty())
            return false;

        GroupRequest request;
        request.kind = GroupRequest::Kind::GetInfo;
        request.gid = gid;
        m_group_requests.insert(msgid, request);

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::GetGroupInfoReq),
                    static_cast<std::uint16_t>(protocol::MessageId::GetGroupInfoRsp), msgid,
                    [this, msgid, gid]()
                        {
                            m_group_requests.remove(msgid);
                            protocol::GroupInfo info;
                            info.gid = gid;
                            emit groupInfoReceived(
                                false, static_cast<int>(protocol::ErrorCode::RequestTimeout), info);
                        });
        return true;
    }

    bool ChatSocket::changeGroupInfo(const QString& gid,
                                     const std::optional<QString>& name,
                                     const std::optional<QString>& avatar,
                                     const std::optional<QString>& announcement)
    {
        if (!isConnected() || !m_logged_in)
        {
            qWarning() << "[chat] 未登录或未连接，无法修改群资料";
            return false;
        }

        if (!isNumericId(gid))
        {
            qWarning() << "[chat] 修改群资料缺少合法的 gid:" << gid;
            return false;
        }

        if (m_group_requests.size() >= kMaxGroupRequestsInFlight)
        {
            qWarning() << "[chat] 群请求积压过多(" << m_group_requests.size()
                << ")，本次修改群资料被丢弃";
            return false;
        }

        /*
         * 可选字段语义（api-group.md §10.2）：
         *   · 不出现 = 保留原值（服务端 COALESCE）；
         *   · 空串   = 清空头像 / 公告（群名空串会被存储过程判非法）；
         * 所以**只有调用方给了值才写字段**，没给就别写 key。
         */
        QJsonObject body;
        body.insert(QStringLiteral("gid"), gid);
        if (name.has_value())
            body.insert(QStringLiteral("name"), *name);
        if (avatar.has_value())
            body.insert(QStringLiteral("avatar"), *avatar);
        if (announcement.has_value())
            body.insert(QStringLiteral("announcement"), *announcement);

        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::GroupInfoChangeReq), body);
        if (msgid.isEmpty())
            return false;

        GroupRequest request;
        request.kind = GroupRequest::Kind::ChangeInfo;
        request.gid = gid;
        request.name = name.value_or(QString());
        request.avatar = avatar.value_or(QString());
        request.announcement = announcement.value_or(QString());
        m_group_requests.insert(msgid, request);

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::GroupInfoChangeReq),
                    static_cast<std::uint16_t>(protocol::MessageId::GroupInfoChangeRsp), msgid,
                    [this, msgid, gid]()
                        {
                            m_group_requests.remove(msgid);
                            emit groupInfoChangeResult(
                                false, static_cast<int>(protocol::ErrorCode::RequestTimeout), gid);
                        });
        return true;
    }

    bool ChatSocket::authFriendRequest(const std::uint64_t requestId, const int state)
    {
        // 服务端只认 1（接受）/ 2（拒绝），其它值会被顶回 1020。本地先挡一层，
        // 省一次往返 —— 这种错是客户端自己写错，不该让服务端来告诉用户。
        if (state != 1 && state != 2)
        {
            qWarning() << "[chat] 非法的好友申请处理状态:" << state;
            return false;
        }

        if (m_auth_in_flight != 0)
        {
            qWarning() << "[chat] 上一个好友申请处理还没回包，本次请求被丢弃";
            return false;
        }

        /*
         * 字段类型按 ServerDocs/api-friend.md §10：
         *   add_friend_request_id 在 1010 请求里是**字符串**、state 也是**字符串**。
         * 与 1009/1012 推送里的数字形态不同，别照抄。
         */
        QJsonObject body;
        body.insert(QStringLiteral("add_friend_request_id"), QString::number(requestId));
        body.insert(QStringLiteral("state"), QString::number(state));

        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::AuthFriendReq), body);
        if (msgid.isEmpty())
            return false;

        m_auth_in_flight = requestId;
        m_auth_state_in_flight = state;

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::AuthFriendReq),
                    static_cast<std::uint16_t>(protocol::MessageId::AuthFriendRsp), msgid,
                    [this]()
                        {
                            // 超时 ≠ 成功也 ≠ 失败：这条申请到底处理了没，谁都说不清。
                            // 把 in-flight 放掉让界面恢复可操作，并提示用户重拉列表核对真实状态。
                            const std::uint64_t id = m_auth_in_flight;
                            const int state = m_auth_state_in_flight;
                            m_auth_in_flight = 0;
                            m_auth_state_in_flight = 0;

                            emit authFriendRequestResult(
                                false, static_cast<int>(protocol::ErrorCode::RequestTimeout), id, state);
                        });
        return true;
    }

    bool ChatSocket::sendHeartbeat()
    {
        if (!isConnected() || !m_logged_in)
        {
            // 未登录/已断开时静默跳过：这时候发心跳没有任何意义，也不该刷警告
            return false;
        }

        /*
         * 2026-10-22 起心跳也走 `HANDLER_PREFIX`，回包会**原样回显 msgid**。
         *
         * 所以不能再发 0 长度 body：服务端解析不出 msgid，回包里就是 null，
         * 这条心跳丢没丢也就无从判断。改发只带编号的最小对象（服务端不读其它字段）。
         */
        const QJsonObject body;
        const QString msgid = sendJsonWithId(
            static_cast<std::uint16_t>(protocol::MessageId::HeartBeatReq), body);
        if (msgid.isEmpty())
            return false;

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::HeartBeatReq),
                    static_cast<std::uint16_t>(protocol::MessageId::HeartBeatRsp), msgid,
                    [this]()
                        {
                            /*
                             * 心跳超时基本等于这一帧被服务端丢了（队列满时 try_send 失败）。
                             * 不重连、不重发：下个周期还会发，重连反而会把正常会话打断。
                             */
                            emit heartbeatResult(
                                false, static_cast<int>(protocol::ErrorCode::RequestTimeout));
                        });
        return true;
    }

bool ChatSocket::checkUserOnline(const QString& uid)
{
    if (uid.trimmed().isEmpty())
    {
        qWarning() << "[chat] 查在线的 uid 为空，请求被丢弃";
        return false;
    }

    /*
     * 必须是**纯数字**的 uint64：服务端 `checkIsUserOnlineHandler`
     * （chatServer/LogicSystem.cpp:617）用 `uid_codec::ParseUid()` 解析，
     * 底层 `std::from_chars` 是整串匹配 —— 任何非数字字符都会直接回 1020。
     * 客户端里唯一会混进来的非数字标识是群会话键 `g<gid>`，它只该活在本地。
     */
    if (!isNumericId(uid))
    {
        qWarning() << "[chat] 查在线的 uid 不是纯数字，请求被丢弃:" << uid
            << "（群会话键 g<gid> 不能当 uid 发出去）";
        return false;
    }

    if (!isConnected() || !m_logged_in)
    {
        qWarning() << "[chat] 未登录，无法查询在线状态:" << uid;
        return false;
    }

    const QString key = uid.trimmed();

    // 已经在队列里/正在查，就当成功 —— 调用方不需要关心去重
    if (key == m_online_in_flight || m_online_queue.contains(key))
        return true;

    m_online_queue.append(key);
    pumpOnlineQueue();
    return true;
}

void ChatSocket::pumpOnlineQueue()
{
    if (!m_online_in_flight.isEmpty() || m_online_queue.isEmpty())
        return;

    const QString uid = m_online_queue.takeFirst();

    QJsonObject body;
    body.insert(QStringLiteral("uid"), uid);

    const QString msgid = sendJsonWithId(
        static_cast<std::uint16_t>(protocol::MessageId::CheckIsUserOnlineReq), body);
    if (!msgid.isEmpty())
    {
        m_online_in_flight = uid;

        /*
         * 回包本身不带 uid，但带 msgid，理论上可以并发查多个人了；这里仍保持串行：
         * 在线状态是"顺带刷新"的低优先级信息，串行足以覆盖界面上的几十个好友。
         *
         * 这一条超时如果不兜住，队列就永久卡住 —— 表现出来是"在线状态的小点再也
         * 不更新了"，而且完全静默。这里不发 userOnlineResult：状态仍旧未知，
         * 重排队尾再试一次比谎报离线诚实。
         */
        expectReply(static_cast<std::uint16_t>(protocol::MessageId::CheckIsUserOnlineReq),
                    static_cast<std::uint16_t>(protocol::MessageId::CheckIsUserOnlineRsp), msgid,
                    [this]()
                        {
                            m_online_in_flight.clear();
                            pumpOnlineQueue();
                        });
        return;
    }

    qWarning() << "[chat] 查询在线状态发送失败:" << uid;
    m_online_queue.clear();
}

void ChatSocket::disconnectFromServer()
{
    reset();
}

void ChatSocket::reset()
    {
        m_connect_timer->stop();
        m_heartbeat_timer->stop();
        clearPendingReplies();
        m_buffer.clear();
        m_logged_in = false;
        m_online_in_flight.clear();
        m_online_queue.clear();
        // 断线时在飞的处理请求也一并作废：否则重连后收了别的东西会拿旧 id 去报结果
        m_auth_in_flight = 0;
        m_auth_state_in_flight = 0;
        // 删除好友同理：不清的话重连后会一直卡在"上一个还没回包"，再也删不了人
        m_del_friend_in_flight.clear();
        // 设置备注 / 编辑资料：同一个道理，重连后必须放行
        m_remark_uid_in_flight.clear();
        m_remark_value_in_flight.clear();
        m_profile_in_flight = false;
        m_profile_name_in_flight.clear();
        m_profile_desc_in_flight.clear();
        // 群管理：按 msgid 存的上下文在断线后不可能再有回包，直接清空
        m_group_requests.clear();

        /*
         * 消息队列同理：连接都没了，排队中的消息不可能再发出去。
         * 这里**不逐条 emit 失败**（断开瞬间刷一串"发送失败"只会盖掉真正的断线提示），
         * 由 ChatController 监听 linkError 统一把"发送中"的气泡标红。
         */
        m_text_queue.clear();
        m_text_in_flight.reset();

        // 拉取 / 确认的排队也一并作废：换连接后消费组是新的，旧批次没有意义
        m_pull_in_flight = false;
        m_ack_queue.clear();
        m_ack_in_flight = false;

        if (m_socket->state() != QAbstractSocket::UnconnectedState)
            m_socket->abort();
    }

    void ChatSocket::onConnected()
    {
        m_connect_timer->stop();
        m_buffer.clear();

        qDebug() << "[chat] 已连接" << m_host << m_port;
        emit connected();
    }

    void ChatSocket::onDisconnected()
    {
        m_connect_timer->stop();
        // 心跳必须在这里停：不然断开后定时器还在跑，每次都撞到
        // sendHeartbeat() 的"未登录"分支白跑
        m_heartbeat_timer->stop();
        clearPendingReplies();
        m_buffer.clear();
        m_online_in_flight.clear();
        m_online_queue.clear();
        // 排队中的消息跟着连接一起作废（重连后不该把上一轮的旧消息偷偷补发出去）
        m_text_queue.clear();
        m_text_in_flight.reset();
        // 拉取 / 确认同理：断线时那批消息的服务端状态已经无法保证，别在重连后补 ACK
        m_pull_in_flight = false;
        m_ack_queue.clear();
        m_ack_in_flight = false;
        // 群管理：断线后这些请求不可能再有回包（超时回调也被 clearPendingReplies 清掉了），
        // 不清就会一直占着 kMaxGroupRequestsInFlight 的名额
        m_group_requests.clear();

        const bool was_logged_in = m_logged_in;
        m_logged_in = false;

        qDebug() << "[chat] 与" << m_host << m_port << "的连接已断开";

        if (was_logged_in)
            emit linkError(tr("与聊天服务器的连接已断开"));
    }

    void ChatSocket::onSocketError(const QAbstractSocket::SocketError error)
    {
        Q_UNUSED(error)

        qWarning() << "[chat] socket 错误:" << m_socket->errorString();

        // 连接阶段的错误由这里上报；已建立连接后的错误交给 onDisconnected
        if (m_socket->state() == QAbstractSocket::ConnectedState)
            return;

        m_connect_timer->stop();
        reset();
        emit connectFailed(m_socket->errorString());
    }

    void ChatSocket::onReadyRead()
    {
        m_buffer.append(m_socket->readAll());
        dispatchBuffer();
    }

    void ChatSocket::dispatchBuffer()
    {
        for (;;)
        {
            if (m_buffer.size() < protocol::HEAD_TOTAL_LEN)
                return; // 头部不完整，继续等

            const auto* head = reinterpret_cast<const unsigned char*>(m_buffer.constData());
            const auto message_id = static_cast<std::uint16_t>((head[0] << 8) | head[1]);
            const auto body_len = static_cast<std::uint16_t>((head[2] << 8) | head[3]);

            /*
             * ⚠ 入站**故意不做** body_len 上限校验（这里原来是 `> MAX_BODY_LEN` 就断连）。
             *
             * 理由：body_len 只有 2 字节，物理最大值就是 65535；而服务端
             * `CSession::SendNode` 写帧头时**不校验长度** —— 好友列表(1021) 这种
             * 随数据量增长的回包超过 2048 是完全可能的（一个好友项约 130 字节，
             * 十来个好友就到顶）。用出站的 2048 去卡入站，结果是一条**合法但长**的
             * 回包被判成非法帧 → reset() 断连，界面只说"连接已断开"，
             * 好友越多越像"网络问题"。（用 2049 字节的合法帧复现过。）
             *
             * 现在：服务端发多少收多少（上限由帧头字段本身决定）。出站方向仍必须守
             * `MAX_BODY_LEN` —— 那是服务端的接收限制，不是同一件事。
             */
            if (body_len > protocol::MAX_BODY_LEN)
            {
                // 不是错误，只是"服务端发得比它自己声明的上限还大"：留一条 INFO 便于定位
                qInfo() << "[chat] 收到超过服务端 2048 约定的长帧: message_id ="
                    << messageIdName(message_id) << "body_len =" << body_len
                    << "（按帧头正常解析，不断连）";
            }

            const int frame_len = protocol::HEAD_TOTAL_LEN + body_len;
            if (m_buffer.size() < frame_len)
                return; // body 未收全，继续等

            const QByteArray body = m_buffer.mid(protocol::HEAD_TOTAL_LEN, body_len);
            m_buffer.remove(0, frame_len);

            qDebug() << "[chat] 收到" << messageIdName(message_id) << QString::fromUtf8(body);
            handleFrame(message_id, body);
        }
    }

    void ChatSocket::handleFrame(const std::uint16_t message_id, const QByteArray& body)
    {
        /*
         * 回包到了先把「等回包」这笔账销掉 —— 统一在这里做，而不是分散到各个 handler
         * 里：那样每加一条新消息都得记得补一行，漏一次就是一次"界面永远转圈"。
         * 服务端主动推的那三帧（1009 / 1012 / 1022）不带 msgid，查不到就自然无事发生。
         *
         * 从 2026-10-22 起回包都带 msgid，认领改为**按编号精确匹配**；
         * 这样同类请求并发在飞时也不会互相销账。
         */
        finishReply(message_id, msgIdOf(body));

        switch (static_cast<protocol::MessageId>(message_id))
    {
        case protocol::MessageId::ChatLoginRsp:
            handleLoginResponse(body);
            break;
        case protocol::MessageId::SearchUserRsp:
            handleSearchUserResponse(body);
            break;
        case protocol::MessageId::AddFriendRsp:
            handleAddFriendResponse(body);
            break;
        case protocol::MessageId::SyncFriendRequestRsp:
            handleFriendRequestList(body);
            break;
        case protocol::MessageId::NotifyAddFriendReq:
            handleNotifyAddFriendRequest(body);
            break;
        case protocol::MessageId::AuthFriendRsp:
            handleAuthFriendResponse(body);
            break;
        case protocol::MessageId::NotifyAuthFriendReq:
            handleNotifyAuthFriend(body);
            break;
        case protocol::MessageId::HeartBeatRsp:
            handleHeartbeatResponse(body);
            break;
        case protocol::MessageId::CheckIsUserOnlineRsp:
            handleUserOnlineResponse(body);
            break;
        case protocol::MessageId::SyncFriendListRsp:
            handleFriendList(body);
            break;
        case protocol::MessageId::TextChatMsgRsp:
            handleTextMessageResponse(body);
            break;
        case protocol::MessageId::NotifyReceiveMessage:
            handleNotifyReceiveMessage(body);
            break;
        case protocol::MessageId::SyncChatMessageRsp:
            handleSyncChatMessages(body);
            break;
        case protocol::MessageId::AckChatMessageRsp:
            handleAckChatMessages(body);
            break;
        case protocol::MessageId::DelFriendRsp:
            handleDelFriendResponse(body);
            break;
        case protocol::MessageId::SetFriendRemarkRsp:
            handleSetFriendRemarkResponse(body);
            break;
        case protocol::MessageId::ChangeUserInfoRsp:
            handleChangeUserInfoResponse(body);
            break;
        case protocol::MessageId::GroupCreateRsp:
            handleGroupCreateResponse(body);
            break;
        case protocol::MessageId::GroupAddMembersRsp:
            handleGroupAddMembersResponse(body);
            break;
        case protocol::MessageId::GroupKickMemberRsp:
            handleGroupKickMemberResponse(body);
            break;
        case protocol::MessageId::GroupDismissRsp:
            handleGroupDismissResponse(body);
            break;
        case protocol::MessageId::GetGroupUserInfoRsp:
            handleGroupMembersResponse(body);
            break;
        case protocol::MessageId::GetGroupInfoRsp:
            handleGroupInfoResponse(body);
            break;
        case protocol::MessageId::GroupInfoChangeRsp:
            handleGroupInfoChangeResponse(body);
            break;
        default:
            // 其余业务消息按阶段推进逐个接入；未知编号丢弃但不断开连接。
            //
            // 注意 `NotifyTextChatMsgReq`(1015) 是**已定义但服务端从不发送**的编号
            // （实际推送走 1022），别把它当成"收到消息"的入口。
            qWarning() << "[chat] 暂未处理的 message_id:" << message_id
                << messageIdName(message_id);
            break;
    }
    }

    void ChatSocket::handleLoginResponse(const QByteArray& body)
    {
        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 登录回包解析失败:" << parse_error.errorString();
            emit loginFailed(static_cast<int>(protocol::ErrorCode::JsonError));
            return;
        }

        const QJsonObject object = document.object();

        // 文档：失败响应只含 error 字段
        const int error = object.value(QStringLiteral("error"))
                                .toInt(static_cast<int>(protocol::ErrorCode::JsonError));
        if (error != static_cast<int>(protocol::ErrorCode::Success))
        {
            qWarning() << "[chat] 登录聊天服失败, error =" << error;
            emit loginFailed(error);
            return;
        }

        const std::optional<std::uint64_t> uid =
            protocol::parseUid(object.value(QStringLiteral("uid")));
        if (!uid.has_value())
        {
            qWarning() << "[chat] 登录回包 uid 非法:" << object;
            emit loginFailed(static_cast<int>(protocol::ErrorCode::JsonError));
            return;
        }

        const QString name = object.value(QStringLiteral("name")).toString();
        const QString token = object.value(QStringLiteral("token")).toString();

        m_logged_in = true;
        qInfo() << "[chat] 登录聊天服成功: uid =" << *uid << "name =" << name;

        // 登录成功就开始心跳。服务端的在线标记是 Redis 里 30 秒 TTL 的 uip_<uid>，
        // 不续期的话别人查你就是 offline，好友申请也不会被推过来。
        if (!m_heartbeat_timer->isActive())
            m_heartbeat_timer->start();

        emit loginSucceeded(*uid, name, token);
    }

    void ChatSocket::handleSearchUserResponse(const QByteArray& body)
    {
        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 搜索用户回包解析失败:" << parse_error.errorString();
            emit searchUserResult(false, {}, static_cast<int>(protocol::ErrorCode::JsonError));
            return;
        }

        const QJsonObject object = document.object();
        const int error = object.value(QStringLiteral("error"))
                                .toInt(static_cast<int>(protocol::ErrorCode::Success));

        const QList<protocol::UserInfo> infos = protocol::parseSearchUserRsp(object);
        if (infos.isEmpty())
        {
            // 服务端没找到用户时只回 {"error":1014}
            qDebug() << "[chat] 未找到该用户, error =" << error;
            emit searchUserResult(false, {}, error);
            return;
        }

        qInfo() << "[chat] 搜索到用户:" << infos.size() << "个命中, 首个 uid =" << infos.first().uid;
        emit searchUserResult(true, infos, error);
    }

    void ChatSocket::handleAddFriendResponse(const QByteArray& body)
    {
        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 好友申请回包解析失败:" << parse_error.errorString();
            emit friendRequestResult(false, static_cast<int>(protocol::ErrorCode::JsonError), 0);
            return;
        }

        const QJsonObject object = document.object();
        const int error = object.value(QStringLiteral("error"))
                                .toInt(static_cast<int>(protocol::ErrorCode::Success));

        if (error != static_cast<int>(protocol::ErrorCode::Success))
        {
            qWarning() << "[chat] 发送好友申请失败, error =" << error;
            emit friendRequestResult(false, error, 0);
            return;
        }

        // 服务端回的是申请记录 id（字符串形式）
        const auto id = protocol::parseUid(object.value(QStringLiteral("add_friend_request_id")));
        if (!id.has_value() || *id == 0)
        {
            /*
             * 成功的 AddFriendRsp 一定带 add_friend_request_id（存储过程把新记录的 id 回给上层）。
             * 缺了说明这条"1006"根本不是正常的申请回包 —— 现实里就发生过：
             * 服务端 checkIsUserOnlineHandler 误用 MESSAGE_ADD_FRIEND_RSP 发查在线回包
             * （`{"error":0,"online":"offline"}`），这里若照单全收，界面会凭空弹一句
             * "好友申请已发送"。按失败上报，至少不会撒谎。
             */
            qWarning() << "[chat] 好友申请回包缺少 add_friend_request_id，按失败处理:" << object;
            emit friendRequestResult(false, static_cast<int>(protocol::ErrorCode::JsonError), 0);
            return;
        }

        qInfo() << "[chat] 好友申请已发送, request id =" << *id;
        emit friendRequestResult(true, error, *id);
    }

    void ChatSocket::handleFriendRequestList(const QByteArray& body)
    {
        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 好友申请列表回包解析失败:" << parse_error.errorString();
            emit friendRequestList({}, static_cast<int>(protocol::ErrorCode::JsonError));
            return;
        }

        const QJsonObject object = document.object();
        const int error = object.value(QStringLiteral("error"))
                                .toInt(static_cast<int>(protocol::ErrorCode::Success));

        const QList<protocol::FriendRequest> requests = protocol::parseFriendRequestList(object);
        qInfo() << "[chat] 收到好友申请列表, 共" << requests.size() << "条";

        emit friendRequestList(requests, error);
    }

    void ChatSocket::handleFriendList(const QByteArray& body)
    {
        /*
         * 1021：{"error":0, "count":"2", "friends":[{uid,name,desc,nick,sex,icon}, ...]}
         *
         * 三个实测到的事实（都已经在 protocol::parseFriendList 里兜住）：
         *   · count 是**字符串**（std::to_string），文档 §8.2 表格写成 number 是错的；
         *   · 没有好友时 friends 是 **null**，不是"字段缺席"；
         *   · 好友资料查不到的，服务端跳过不发。
         */
        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 好友列表回包解析失败:" << parse_error.errorString();
            emit friendListResult({}, static_cast<int>(protocol::ErrorCode::JsonError));
            return;
        }

        const QJsonObject object = document.object();
        const int error = object.value(QStringLiteral("error"))
                                .toInt(static_cast<int>(protocol::ErrorCode::JsonError));

        const QList<protocol::FriendInfo> friends = protocol::parseFriendList(object);
        qInfo() << "[chat] 收到好友列表, 共" << friends.size() << "位, error =" << error;

        emit friendListResult(friends, error);
    }

    void ChatSocket::handleDelFriendResponse(const QByteArray& body)
    {
        /*
         * 1028：{"error":0, "uid":"10002"}
         *
         * 服务端把请求里的 uid **原样回显**（成功、失败都带），所以优先用回包认领；
         * 回显为空串 / 老服务端缺失时，退回请求侧记下的那一发。
         */
        const QString in_flight = m_del_friend_in_flight;
        m_del_friend_in_flight.clear();

        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 删除好友回包解析失败:" << parse_error.errorString();
            emit friendDeleteResult(false, static_cast<int>(protocol::ErrorCode::JsonError),
                                    in_flight);
            return;
        }

        const QJsonObject object = document.object();
        const int error = object.value(QStringLiteral("error"))
                                .toInt(static_cast<int>(protocol::ErrorCode::JsonError));

        const auto echoed = protocol::parseUid(object.value(QStringLiteral("uid")));
        const QString uid = echoed.has_value() && *echoed != 0
                                ? QString::number(*echoed)
                                : in_flight;

        if (uid.isEmpty())
        {
            // 既没回显、也没有在飞的请求 → 认不出删的是谁，别乱动好友列表
            qWarning() << "[chat] 收到无法认领的删除好友回包，已忽略:" << object;
            return;
        }

        if (echoed.has_value() && !in_flight.isEmpty() && uid != in_flight)
        {
            qWarning() << "[chat] 1028 回显的 uid 与在飞请求不一致: echoed =" << uid
                << "in-flight =" << in_flight;
        }

        if (error != static_cast<int>(protocol::ErrorCode::Success))
        {
            // 1022 = 本来就不是好友（关系不存在），1013 = 数据库 / Redis 不可用
            qWarning() << "[chat] 删除好友失败, uid =" << uid << "error =" << error;
            emit friendDeleteResult(false, error, uid);
            return;
        }

        qInfo() << "[chat] 已删除好友, uid =" << uid;
        emit friendDeleteResult(true, error, uid);
    }

    void ChatSocket::handleSetFriendRemarkResponse(const QByteArray& body)
    {
        /*
         * 1030：{"error":0, "id":"r-10002"}
         *
         * 只有 error 与回显的 id —— 没有好友 uid、也没有新备注。所以成功时要用的
         * "改的是谁、改成了什么"只能从请求侧记下的那份取（串行发送，一定是同一发）。
         */
        const QString uid = m_remark_uid_in_flight;
        const QString remark = m_remark_value_in_flight;
        m_remark_uid_in_flight.clear();
        m_remark_value_in_flight.clear();

        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 设置备注回包解析失败:" << parse_error.errorString();
            emit friendRemarkResult(false, static_cast<int>(protocol::ErrorCode::JsonError),
                                    uid, remark);
            return;
        }

        const QJsonObject object = document.object();
        /*
         * 缺 `error` 一律按失败处理（服务端 Redis 异常时确实可能只回 id）。
         * 这里用 JsonError 当缺省值 —— 它至少会让界面说"失败"，而不是静默当成功。
         */
        const int error = object.value(QStringLiteral("error"))
                                .toInt(static_cast<int>(protocol::ErrorCode::JsonError));

        if (error != static_cast<int>(protocol::ErrorCode::Success))
        {
            qWarning() << "[chat] 设置好友备注失败, uid =" << uid << "error =" << error;
            emit friendRemarkResult(false, error, uid, remark);
            return;
        }

        qInfo() << "[chat] 已设置好友备注, uid =" << uid << "长度 =" << remark.size();
        emit friendRemarkResult(true, error, uid, remark);
    }

    void ChatSocket::handleChangeUserInfoResponse(const QByteArray& body)
    {
        /*
         * 1032：{"error":0, "id":"u-xxx"}
         *
         * 与 1030 同构：只有 error + 回显 id，提交了什么值得从请求侧取回。
         */
        const QString name = m_profile_name_in_flight;
        const QString desc = m_profile_desc_in_flight;
        const int sex = m_profile_sex_in_flight;
        m_profile_in_flight = false;
        m_profile_name_in_flight.clear();
        m_profile_desc_in_flight.clear();

        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 编辑资料回包解析失败:" << parse_error.errorString();
            emit userInfoChangeResult(false, static_cast<int>(protocol::ErrorCode::JsonError),
                                      name, desc, sex);
            return;
        }

        const QJsonObject object = document.object();
        const int error = object.value(QStringLiteral("error"))
                                .toInt(static_cast<int>(protocol::ErrorCode::JsonError));

        if (error != static_cast<int>(protocol::ErrorCode::Success))
        {
            qWarning() << "[chat] 编辑资料失败, error =" << error;
            emit userInfoChangeResult(false, error, name, desc, sex);
            return;
        }

        qInfo() << "[chat] 资料已更新, 新昵称 =" << name;
        emit userInfoChangeResult(true, error, name, desc, sex);
    }

    void ChatSocket::handleNotifyAddFriendRequest(const QByteArray& body)
    {
        /*
         * 服务端主动推送（LogicSystem::sendFriendRequestHandler → CSession::sendAuthFriendRequest）：
         *     {"error":0, "add_friend_request_id":13}
         *
         * 两个容易踩的点：
         *   1. 推送体里**没有**对方 uid / 昵称 / 验证信息，只有一个申请记录 id ——
         *      它只能当"有新申请"的信号用，详情必须回拉 SyncFriendRequestReq。
         *      （顺带一提：这里服务端把 id 当 **JSON 数字**发，而 AddFriendRsp 里
         *       是 to_string 之后的字符串；parseUid 两种都认。）
         *   2. 这是推送，没有请求可对应、也没有回包可发：解析失败只能丢弃，
         *      绝不能因此断开连接 —— 一次脏帧不该毁掉后面所有消息。
         */
        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 好友申请推送解析失败:" << parse_error.errorString()
                << QString::fromUtf8(body);
            return;
        }

        const QJsonObject object = document.object();
        const int error = object.value(QStringLiteral("error"))
                                .toInt(static_cast<int>(protocol::ErrorCode::JsonError));
        if (error != static_cast<int>(protocol::ErrorCode::Success))
        {
            qWarning() << "[chat] 好友申请推送带错误码, error =" << error;
            return;
        }

        // 和 AddFriendRsp 同一套判据：成功的推送一定带申请记录 id。
        // 缺了就说明这个 1009 不是我们以为的东西（服务端历史上就有用错 message_id 的先例，
        // 见 handleAddFriendResponse 里那段关于 1006/1019 的说明），宁可不报也不谎报。
        const auto id = protocol::parseUid(object.value(QStringLiteral("add_friend_request_id")));
        if (!id.has_value() || *id == 0)
        {
            qWarning() << "[chat] 好友申请推送缺少 add_friend_request_id，已忽略:" << object;
            return;
        }

        qInfo() << "[chat] 收到好友申请推送, request id =" << *id;
        emit friendRequestNotified(*id);
    }

    void ChatSocket::handleAuthFriendResponse(const QByteArray& body)
    {
        /*
         * 1011：{"error":0, "add_friend_request_id":"123"}
         *
         * 服务端从 2026-10-17 起会把请求里的编号**原样回显**（成功、失败都带），
         * 所以结果可以靠回包自己认领 —— 不再依赖"请求侧记下的那一发"。
         * 但仍然保留兜底：
         *   · 老服务端 / 请求里没带编号时，回显是空串 → 用 in-flight 的编号；
         *   · state 始终不回显 → 只能从请求侧取（没有在飞请求时为 0 = 未知）。
         */
        const std::uint64_t in_flight_id = m_auth_in_flight;
        const int in_flight_state = m_auth_state_in_flight;
        m_auth_in_flight = 0;
        m_auth_state_in_flight = 0;

        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 好友申请处理回包解析失败:" << parse_error.errorString();
            emit authFriendRequestResult(false, static_cast<int>(protocol::ErrorCode::JsonError),
                                         in_flight_id, in_flight_state);
            return;
        }

        const QJsonObject object = document.object();
        const int error = object.value(QStringLiteral("error"))
                                .toInt(static_cast<int>(protocol::ErrorCode::JsonError));

        const auto echoed = protocol::parseUid(object.value(QStringLiteral("add_friend_request_id")));
        const std::uint64_t request_id = echoed.value_or(0) != 0 ? *echoed : in_flight_id;

        if (request_id == 0)
        {
            // 既没回显、也没有在飞的请求 → 认不出是哪一条，宁可丢掉也不安到别人头上
            qWarning() << "[chat] 收到无法认领的好友申请处理回包，已忽略:" << object;
            return;
        }

        if (echoed.has_value() && in_flight_id != 0 && *echoed != in_flight_id)
        {
            // 服务端回的编号跟我们记的对不上：以回包为准，但这条日志值得看一眼
            qWarning() << "[chat] 1011 回显的编号与在飞请求不一致: echoed =" << *echoed
                << "in-flight =" << in_flight_id;
        }

        // state 不回显：没有在飞请求时就是"未知 0"，调用方据此走"重新拉列表"而不是猜结果
        const int state = in_flight_state;

        if (error != static_cast<int>(protocol::ErrorCode::Success))
        {
            qWarning() << "[chat] 处理好友申请失败, request id =" << request_id
                << "state =" << state << "error =" << error;
            emit authFriendRequestResult(false, error, request_id, state);
            return;
        }

        qInfo() << "[chat] 好友申请已处理, request id =" << request_id
            << (state == 1 ? "(接受)" : (state == 2 ? "(拒绝)" : "(状态未知)"));
        emit authFriendRequestResult(true, error, request_id, state);
    }

    void ChatSocket::handleNotifyAuthFriend(const QByteArray& body)
    {
        /*
         * 对方处理了我发出的申请（只推给在线的申请人）：
         *   {"error":0, "add_friend_request_id":"19", "state":1}
         *
         * ⚠ 文档 §6 把编号写成"数字"，但**服务端实测是字符串**（2026-10-17，裸帧核对）。
         *   parseUid 两种都吃，所以这里不纠结类型；哪天服务端改成数字也不会坏。
         *
         * 推送没有回包可发，解析失败只能丢弃 —— 不能因此断连接。
         */
        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 好友申请结果推送解析失败:" << parse_error.errorString()
                << QString::fromUtf8(body);
            return;
        }

        const QJsonObject object = document.object();
        const auto id = protocol::parseUid(object.value(QStringLiteral("add_friend_request_id")));
        const int state = object.value(QStringLiteral("state")).toInt(0);

        if (!id.has_value() || *id == 0 || (state != 1 && state != 2))
        {
            qWarning() << "[chat] 好友申请结果推送字段不完整，已忽略:" << object;
            return;
        }

        qInfo() << "[chat] 对方已处理我的好友申请, request id =" << *id
            << (state == 1 ? "(接受)" : "(拒绝)");
        emit friendRequestHandled(*id, state);
    }

    void ChatSocket::handleHeartbeatResponse(const QByteArray& body)
    {
        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 心跳回包解析失败:" << parse_error.errorString();
            emit heartbeatResult(false, static_cast<int>(protocol::ErrorCode::JsonError));
            return;
        }

        const int error = document.object().value(QStringLiteral("error"))
                                  .toInt(static_cast<int>(protocol::ErrorCode::JsonError));

        if (error != static_cast<int>(protocol::ErrorCode::Success))
        {
            // 续期失败＝在线状态已经开始倒计时，值得一条警告
            qWarning() << "[chat] 心跳续期失败, error =" << error;
            emit heartbeatResult(false, error);
            return;
        }

        qDebug() << "[chat] 心跳续期成功";
        emit heartbeatResult(true, error);
    }

    void ChatSocket::handleUserOnlineResponse(const QByteArray& body)
    {
        // 超时已经把这发作废了、或者根本没有在飞的请求 → 这条回包无人认领，丢掉。
        // 宁可少一个状态，也不能把它安到别人头上。
        if (m_online_in_flight.isEmpty())
        {
            qWarning() << "[chat] 收到没有对应请求的在线状态回包，已忽略";
            return;
        }

        // 不管解析成功与否都要把 in-flight 清掉并放行下一个，
        // 否则一次脏回包就能把整条队列卡死
        const QString uid = m_online_in_flight;
        m_online_in_flight.clear();

        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);

        bool online = false;
        int error = static_cast<int>(protocol::ErrorCode::JsonError);

        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 在线状态回包解析失败:" << parse_error.errorString();
        }
        else
        {
            const QJsonObject object = document.object();
            error = object.value(QStringLiteral("error"))
                          .toInt(static_cast<int>(protocol::ErrorCode::JsonError));
            const std::optional<bool> state = protocol::parseUserOnlineRsp(object);
            online = state.value_or(false);
        }

        const std::uint64_t target = uid.toULongLong();
        qDebug() << "[chat] 用户在线状态: uid =" << uid << "online =" << online
            << "error =" << error;

        emit userOnlineResult(target, online, error);

        pumpOnlineQueue();
    }

    void ChatSocket::handleTextMessageResponse(const QByteArray& body)
    {
        /*
         * 1014：{"error":0,"msg_id":"c-1"}
         *
         * msg_id 是服务端**原样回显**的客户端标识（成功、失败都带）。
         * 发送是串行的，所以"在飞的那一条"本来就是唯一候选；回显值再核对一遍，
         * 是为了万一服务端改了回显逻辑（或回了别人的 id）时能留下证据，
         * 而不是安静地把状态标到错误的气泡上。
         */
        if (!m_text_in_flight.has_value())
        {
            // 超时已经把这发作废了、或者根本没有在飞的请求 → 无人认领，丢掉。
            // 宁可少一条状态，也不能把它安到别的消息头上。
            qWarning() << "[chat] 收到没有对应请求的消息发送回包，已忽略:"
                << QString::fromUtf8(body);
            return;
        }

        const OutgoingText sent = *m_text_in_flight;
        m_text_in_flight.reset();

        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 消息发送回包解析失败:" << parse_error.errorString();
            emit textMessageResult(false, static_cast<int>(protocol::ErrorCode::JsonError),
                                   sent.msg_id, sent.to_uid, sent.body);
            pumpTextQueue();
            return;
        }

        const QJsonObject object = document.object();
        const int error = object.value(QStringLiteral("error"))
                                  .toInt(static_cast<int>(protocol::ErrorCode::JsonError));

        const QString echoed = object.value(QStringLiteral("msg_id")).toString();
        if (echoed.isEmpty())
        {
            // 文档：请求里缺失 msg_id 时服务端回显空串。我们用的一直是必填的 id，
            // 所以这里更像是"服务端还是老版本"——按在飞请求认领，但留一条警告。
            qWarning() << "[chat] 1014 没带 msg_id（服务端版本较旧？），按在飞请求认领:"
                << sent.msg_id;
        }
        else if (echoed != sent.msg_id)
        {
            // 串行发送下这是不可能的（只有一条在飞），说明服务端回显有问题。
            // 仍按**发出时**的 id 上报：界面要的是"我刚发的那条气泡"，
            // 回一个我们不认识的 id 只会让它永远停在「发送中」。
            qWarning() << "[chat] 1014 回显的 msg_id 与在飞请求不一致: echoed ="
                << echoed << "in-flight =" << sent.msg_id;
        }

        if (error != static_cast<int>(protocol::ErrorCode::Success))
        {
            // 能走到这里的服务端错误只有 1020（to_uid / type 非法）；
            // 其余 1001/1013 之类属于服务端内部异常，提示里照原样透出 code。
            qWarning() << "[chat] 消息发送失败, to_uid =" << sent.to_uid
                << "msg_id =" << sent.msg_id << "error =" << error;
            emit textMessageResult(false, error, sent.msg_id, sent.to_uid, sent.body);
            pumpTextQueue();
            return;
        }

        /*
         * error == 0 的语义是"已写入对方离线消息流"（api-chat.md §3.3），
         * **不代表对端收到** —— 界面上别拿它当"已读"。
         */
        qInfo() << "[chat] 消息已写入离线消息流, to_uid =" << sent.to_uid
            << "msg_id =" << sent.msg_id << "body 长度 =" << sent.body.size();
        emit textMessageResult(true, error, sent.msg_id, sent.to_uid, sent.body);
        pumpTextQueue();
    }

    void ChatSocket::handleNotifyReceiveMessage(const QByteArray& body)
    {
        /*
         * 1022：{"error":0}
         *
         * 这是"你有新消息"的信号，**不含**发送者 / 类型 / 内容；服务端也没有
         * 消息拉取接口（api-chat.md §6），所以这里拿不到消息本身。
         * 推送没有回包可发，解析失败只能丢弃 —— 一次脏帧不该毁掉后面的消息。
         */
        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 新消息推送解析失败:" << parse_error.errorString()
                << QString::fromUtf8(body);
            return;
        }

        const int error = document.object().value(QStringLiteral("error"))
                                  .toInt(static_cast<int>(protocol::ErrorCode::JsonError));
        if (error != static_cast<int>(protocol::ErrorCode::Success))
        {
            qWarning() << "[chat] 新消息推送带错误码, error =" << error;
            return;
        }

        qInfo() << "[chat] 收到新消息通知（推送不含内容，需要发 1023 拉取）";
        emit messageReceived();
    }

    void ChatSocket::handleSyncChatMessages(const QByteArray& body)
    {
        /*
         * 1024：{"error":0, "count":"2", "msgs":[{type,from_uid,body,id}, ...]}
         *
         * 没有消息时服务端**不带 msgs 字段**，count 为 "0"（字符串）。
         *
         * ⚠ 服务端按 **PEL 优先**投递：未确认的消息会在下一次拉取时重复返回
         *   （at-least-once）。去重交给 ChatController（按 msgs[].id），
         *   这里只管把这一批原样上报。
         *   迟到的回包也照常处理 —— 反正下一轮 PEL 还会重投，能早一步是一步。
         */
        m_pull_in_flight = false;

        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 拉取消息回包解析失败:" << parse_error.errorString();
            emit chatMessagesReceived({}, static_cast<int>(protocol::ErrorCode::JsonError));
            return;
        }

        const QJsonObject object = document.object();
        const int error = object.value(QStringLiteral("error"))
                                  .toInt(static_cast<int>(protocol::ErrorCode::JsonError));

        const QList<protocol::ChatMessage> messages = protocol::parseChatMessages(object);
        if (!messages.isEmpty())
        {
            qInfo() << "[chat] 拉到" << messages.size() << "条消息, 首条 from_uid ="
                << messages.first().fromUid;
        }

        emit chatMessagesReceived(messages, error);
    }

    void ChatSocket::handleAckChatMessages(const QByteArray& body)
    {
        // 1026：{"error":0} —— 只有 error。谁确认的不重要，按批 FIFO 记一笔即可。
        m_ack_in_flight = false;

        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 确认消息回包解析失败:" << parse_error.errorString();
            emit chatMessagesAcked(false, static_cast<int>(protocol::ErrorCode::JsonError));
            pumpAckQueue();
            return;
        }

        const int error = document.object().value(QStringLiteral("error"))
                                  .toInt(static_cast<int>(protocol::ErrorCode::JsonError));

        if (error != static_cast<int>(protocol::ErrorCode::Success))
        {
            // 失败 = 那批消息还在 PEL 里。现在的服务端会**重投**，所以不算丢；
            // 调用方（ChatController）会隔一会儿再拉一次，把 PEL 抽干。
            qWarning() << "[chat] 确认消息失败（消息仍在 PEL，会被重投）, error =" << error;
            emit chatMessagesAcked(false, error);
            pumpAckQueue();
            return;
        }

        qDebug() << "[chat] 消息已确认（服务端 XACK + XDEL）";
        emit chatMessagesAcked(true, error);
        pumpAckQueue();
    }

    ChatSocket::GroupRequest ChatSocket::takeGroupRequest(const QByteArray& body)
    {
        const QString msgid = msgIdOf(body);

        if (!msgid.isEmpty())
        {
            const auto it = m_group_requests.constFind(msgid);
            if (it != m_group_requests.constEnd())
                return m_group_requests.take(msgid);
        }

        /*
         * 回包没带编号（老服务端 / 服务端某条异常路径）：
         * 只有一条在飞时它必然就是这条，直接认领 —— 和 m_pending 的 FIFO 兜底同理。
         */
        if (m_group_requests.size() == 1)
        {
            qDebug() << "[chat] 群回包缺少可用的 msgid，按唯一在飞请求认领";
            const QString only_key = m_group_requests.constBegin().key();
            return m_group_requests.take(only_key);
        }

        qWarning() << "[chat] 群回包无法匹配请求上下文, msgid =" << msgid
            << "在飞条数 =" << m_group_requests.size();
        return {};
    }

    void ChatSocket::handleGroupCreateResponse(const QByteArray& body)
    {
        // 1034：{"error":0,"msgid":"r-3","group_id":"20001"} —— 唯一回带 group id 的群回包
        // 按 msgid 取回请求侧上下文；回包不带编号时退回"唯一在飞的那条"
        const GroupRequest request = takeGroupRequest(body);

        const std::optional<int> error = replyErrorCode(body);
        if (!error.has_value())
        {
            /*
             * 没有 error 字段：服务端把群主写进 Redis 时抛了异常（§4.2 最后一条）。
             * MySQL 里可能已经建好了群，但客户端拿不到 group_id，只能当失败 ——
             * 界面上让用户重试，比假装成功、然后收到一个"群里没人"要诚实。
             */
            qWarning() << "[chat] 创建群聊回包缺少 error 字段（服务端异常逃逸），按失败处理";
            emit groupCreated(false, static_cast<int>(protocol::ErrorCode::JsonError),
                              {}, request.name);
            return;
        }

        if (*error != static_cast<int>(protocol::ErrorCode::Success))
        {
            qWarning() << "[chat] 创建群聊失败, error =" << *error;
            emit groupCreated(false, *error, {}, request.name);
            return;
        }

        const QString gid = stringField(body, QStringLiteral("group_id"));
        if (gid.isEmpty())
        {
            // 成功却没有 group_id：后续拉人 / 发消息都没法进行，同样只能算失败
            qWarning() << "[chat] 创建群聊成功但回包没有 group_id";
            emit groupCreated(false, static_cast<int>(protocol::ErrorCode::JsonError),
                              {}, request.name);
            return;
        }

        qInfo() << "[chat] 群聊已创建, group_id =" << gid << "name =" << request.name;
        emit groupCreated(true, *error, gid, request.name);
    }

    void ChatSocket::handleGroupAddMembersResponse(const QByteArray& body)
    {
        // 1036：{"error":0,"msgid":"r-4"} —— 不回传入群名单
        // 按 msgid 取回请求侧上下文；回包不带编号时退回"唯一在飞的那条"
        const GroupRequest request = takeGroupRequest(body);

        const std::optional<int> error = replyErrorCode(body);
        if (!error.has_value())
        {
            qWarning() << "[chat] 拉人入群回包缺少 error 字段（空成员列表触发 Redis 异常），按失败处理";
            emit groupMembersAdded(false, static_cast<int>(protocol::ErrorCode::JsonError),
                                   request.gid, request.members);
            return;
        }

        if (*error != static_cast<int>(protocol::ErrorCode::Success))
        {
            qWarning() << "[chat] 拉人入群失败, gid =" << request.gid << "error =" << *error;
            emit groupMembersAdded(false, *error, request.gid, request.members);
            return;
        }

        qInfo() << "[chat] 拉人入群成功, gid =" << request.gid
            << "提交" << request.members.size() << "人";
        emit groupMembersAdded(true, *error, request.gid, request.members);
    }

    void ChatSocket::handleGroupKickMemberResponse(const QByteArray& body)
    {
        // 1038：{"error":0,"msgid":"r-5"}
        // 按 msgid 取回请求侧上下文；回包不带编号时退回"唯一在飞的那条"
        const GroupRequest request = takeGroupRequest(body);

        const std::optional<int> error = replyErrorCode(body);
        if (!error.has_value())
        {
            qWarning() << "[chat] 踢人回包缺少 error 字段，按失败处理";
            emit groupMemberKicked(false, static_cast<int>(protocol::ErrorCode::JsonError),
                                   request.gid, request.target_uid);
            return;
        }

        if (*error != static_cast<int>(protocol::ErrorCode::Success))
        {
            // 1014 在这里表示"目标用户不在群内"，不是"用户不存在"
            qWarning() << "[chat] 踢人失败, gid =" << request.gid
                << "uid =" << request.target_uid << "error =" << *error;
            emit groupMemberKicked(false, *error, request.gid, request.target_uid);
            return;
        }

        qInfo() << "[chat] 已踢出成员, gid =" << request.gid << "uid =" << request.target_uid;
        emit groupMemberKicked(true, *error, request.gid, request.target_uid);
    }

    void ChatSocket::handleGroupDismissResponse(const QByteArray& body)
    {
        // 1040：{"error":0,"msgid":"r-6"}
        // 按 msgid 取回请求侧上下文；回包不带编号时退回"唯一在飞的那条"
        const GroupRequest request = takeGroupRequest(body);

        const std::optional<int> error = replyErrorCode(body);
        if (!error.has_value())
        {
            qWarning() << "[chat] 解散群聊回包缺少 error 字段（DEL 失败逃逸），按失败处理";
            emit groupDismissed(false, static_cast<int>(protocol::ErrorCode::JsonError),
                                request.gid);
            return;
        }

        if (*error != static_cast<int>(protocol::ErrorCode::Success))
        {
            // ⚠ 群不存在时服务端回的是 1013 而不是 1023（§7.3），文案别只写"服务器不可用"
            qWarning() << "[chat] 解散群聊失败, gid =" << request.gid << "error =" << *error;
            emit groupDismissed(false, *error, request.gid);
            return;
        }

        qInfo() << "[chat] 群聊已解散, gid =" << request.gid;
        emit groupDismissed(true, *error, request.gid);
    }

    void ChatSocket::handleGroupMembersResponse(const QByteArray& body)
    {
        // 1042：{"error":0,"msgid":"r-7","mb_infos":[...]}
        // 按 msgid 取回请求侧上下文；回包不带编号时退回"唯一在飞的那条"
        const GroupRequest request = takeGroupRequest(body);

        const std::optional<int> error = replyErrorCode(body);
        if (!error.has_value())
        {
            qWarning() << "[chat] 群成员回包缺少 error 字段，按失败处理";
            emit groupMembersReceived(false, static_cast<int>(protocol::ErrorCode::JsonError),
                                      request.gid, {});
            return;
        }

        if (*error != static_cast<int>(protocol::ErrorCode::Success))
        {
            /*
             * 1024 有两个意思（§8.2）：我不在群里，**或者 Redis 成员集合丢了**。
             * 后者在服务端重启 / 清库后很常见，界面上只能提示"权限不足"。
             */
            qWarning() << "[chat] 获取群成员失败, gid =" << request.gid << "error =" << *error;
            emit groupMembersReceived(false, *error, request.gid, {});
            return;
        }

        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        const QList<protocol::GroupMember> members =
            parse_error.error == QJsonParseError::NoError && document.isObject()
                ? protocol::parseGroupMembers(document.object())
                : QList<protocol::GroupMember>{};

        qInfo() << "[chat] 群成员已获取, gid =" << request.gid << "人数 =" << members.size();
        emit groupMembersReceived(true, *error, request.gid, members);
    }

    void ChatSocket::handleGroupInfoResponse(const QByteArray& body)
    {
        // 1044：{"error":0,"msgid":"r-8","name":"...","avatar":"","owner_uid":"...",...}
        const GroupRequest request = takeGroupRequest(body);

        protocol::GroupInfo info;
        info.gid = request.gid; // 1044 不回群号，用请求上下文补

        const std::optional<int> error = replyErrorCode(body);
        if (!error.has_value())
        {
            qWarning() << "[chat] 群资料回包缺少 error 字段，按失败处理";
            emit groupInfoReceived(false, static_cast<int>(protocol::ErrorCode::JsonError), info);
            return;
        }

        if (*error != static_cast<int>(protocol::ErrorCode::Success))
        {
            // 1023 = 群不存在；1013 = 数据库不可用；1020 = gid 非法
            qWarning() << "[chat] 获取群资料失败, gid =" << request.gid << "error =" << *error;
            emit groupInfoReceived(false, *error, info);
            return;
        }

        QJsonParseError parse_error{};
        const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
        if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        {
            qWarning() << "[chat] 群资料回包 JSON 解析失败:" << parse_error.errorString();
            emit groupInfoReceived(false, static_cast<int>(protocol::ErrorCode::JsonError), info);
            return;
        }

        const protocol::GroupInfo parsed = protocol::parseGroupInfo(document.object());
        info = parsed;
        info.gid = request.gid;
        if (!info.valid)
        {
            // 成功回包却没有群名：服务端 group.name 是 NOT NULL，正常不会发生
            qWarning() << "[chat] 群资料回包缺少 name，按失败处理";
            emit groupInfoReceived(false, static_cast<int>(protocol::ErrorCode::JsonError), info);
            return;
        }

        qInfo() << "[chat] 群资料已获取, gid =" << info.gid << "name =" << info.name
            << "成员数 =" << info.memberCount;
        emit groupInfoReceived(true, *error, info);
    }

    void ChatSocket::handleGroupInfoChangeResponse(const QByteArray& body)
    {
        // 1046：{"error":0,"msgid":"r-9"} —— 只回 error，不回新资料
        const GroupRequest request = takeGroupRequest(body);

        const std::optional<int> error = replyErrorCode(body);
        if (!error.has_value())
        {
            /*
             * 存储过程 -10 / -11（数据库异常）时处理器直接 co_return，
             * 回包只有 msgid（api-group.md §10.3）。客户端必须当失败，
             * 不能因为"没有 error 就当成功"把界面骗过去。
             */
            qWarning() << "[chat] 修改群资料回包缺少 error 字段（数据库异常），按失败处理";
            emit groupInfoChangeResult(false, static_cast<int>(protocol::ErrorCode::JsonError),
                                       request.gid);
            return;
        }

        if (*error != static_cast<int>(protocol::ErrorCode::Success))
        {
            // 1020 = 群名为空串 / gid 非法；1023 = 群不存在；1024 = 不是群主或管理员
            qWarning() << "[chat] 修改群资料失败, gid =" << request.gid << "error =" << *error;
            emit groupInfoChangeResult(false, *error, request.gid);
            return;
        }

        qInfo() << "[chat] 群资料已修改, gid =" << request.gid;
        emit groupInfoChangeResult(true, *error, request.gid);
    }

    QString ChatSocket::sendJsonWithId(const std::uint16_t message_id, const QJsonObject& body)
    {
        if (!isConnected())
        {
            qWarning() << "[chat] 未连接，消息被丢弃:" << messageIdName(message_id);
            return {};
        }

        /*
         * 统一注入请求编号：服务端在所有响应里原样回显（缺失时为 null），
         * 客户端靠它把回包和请求对上号、并判断哪条请求被服务端丢了。
         */
        const QString msgid = nextMsgId();

        QJsonObject payload_body = body;
        payload_body.insert(QStringLiteral("msgid"), msgid);

        const QByteArray payload = QJsonDocument(payload_body).toJson(QJsonDocument::Compact);
        if (!sendFrame(message_id, payload))
            return {};

        return msgid;
    }

    bool ChatSocket::sendFrame(const std::uint16_t message_id, const QByteArray& payload)
    {
        if (!isConnected())
        {
            qWarning() << "[chat] 未连接，消息被丢弃:" << messageIdName(message_id);
            return false;
        }

        if (payload.size() > protocol::MAX_BODY_LEN)
        {
            qWarning() << "[chat] body 过长:" << payload.size() << ">" << protocol::MAX_BODY_LEN;
            return false;
        }

        // 帧头手工拼大端，避免依赖 QDataStream 的隐式行为：
        //   [message_id:2 大端][body_len:2 大端][body]
        QByteArray frame;
        frame.reserve(protocol::HEAD_TOTAL_LEN + payload.size());

        const auto len_value = static_cast<std::uint16_t>(payload.size());
        frame.append(static_cast<char>((message_id >> 8) & 0xFF));
        frame.append(static_cast<char>(message_id & 0xFF));
        frame.append(static_cast<char>((len_value >> 8) & 0xFF));
        frame.append(static_cast<char>(len_value & 0xFF));
        frame.append(payload);

        if (m_socket->write(frame) != frame.size())
        {
            qWarning() << "[chat] 写入失败:" << m_socket->errorString();
            return false;
        }
        m_socket->flush();

        qDebug() << "[chat] 发送" << messageIdName(message_id)
            << (payload.isEmpty() ? QStringLiteral("(无 body)") : QString::fromUtf8(payload));
        return true;
    }
} // namespace mychat
