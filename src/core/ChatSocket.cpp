#include "ChatSocket.h"

#include <QDebug>
#include <QElapsedTimer>
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

                        qWarning() << "[chat] 等待回包超时:" << messageIdName(entry.reply_id)
                            << "(请求" << messageIdName(entry.request_id) << ")";
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
                                 std::function<void()> onTimeout)
    {
        m_pending.append(PendingReply{
            requestId, replyId, m_clock.elapsed() + kReplyTimeoutMs, std::move(onTimeout)
        });
        armReplyTimer();
    }

    void ChatSocket::finishReply(const std::uint16_t replyId)
    {
        // FIFO：同型号的回包按发送顺序认领先发的那一条
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

        if (!sendJson(static_cast<std::uint16_t>(protocol::MessageId::ChatLoginReq), body))
            return false;

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::ChatLoginReq),
                    static_cast<std::uint16_t>(protocol::MessageId::ChatLoginRsp),
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

    if (!sendJson(static_cast<std::uint16_t>(protocol::MessageId::SearchUserReq), body))
        return false;

    expectReply(static_cast<std::uint16_t>(protocol::MessageId::SearchUserReq),
                static_cast<std::uint16_t>(protocol::MessageId::SearchUserRsp),
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

    if (!sendJson(static_cast<std::uint16_t>(protocol::MessageId::AddFriendReq), body))
        return false;

    expectReply(static_cast<std::uint16_t>(protocol::MessageId::AddFriendReq),
                static_cast<std::uint16_t>(protocol::MessageId::AddFriendRsp),
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

        if (!sendJson(static_cast<std::uint16_t>(protocol::MessageId::SyncFriendRequestReq), body))
            return false;

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::SyncFriendRequestReq),
                    static_cast<std::uint16_t>(protocol::MessageId::SyncFriendRequestRsp),
                    [this]()
                        {
                            emit friendRequestList(
                                {}, static_cast<int>(protocol::ErrorCode::RequestTimeout));
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

        if (!sendJson(static_cast<std::uint16_t>(protocol::MessageId::AuthFriendReq), body))
            return false;

        m_auth_in_flight = requestId;
        m_auth_state_in_flight = state;

        expectReply(static_cast<std::uint16_t>(protocol::MessageId::AuthFriendReq),
                    static_cast<std::uint16_t>(protocol::MessageId::AuthFriendRsp),
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

    // 服务端 heartbeatHandler 整个忽略了 message_data，规格也是"不带数据"，
    // 所以这里发 body_len = 0 的空帧。
    // （实测过：体长 0 与 "{}" 服务端都正常回 HeartBeatRsp —— 服务端拆帧在
    //   message_len == 0 时会走 AsyncReadBody(0)，那条路是通的，
    //   用裸 socket 探过两个 ChatServer 实例。）
    return sendFrame(static_cast<std::uint16_t>(protocol::MessageId::HeartBeatReq), {});
}

bool ChatSocket::checkUserOnline(const QString& uid)
{
    if (uid.trimmed().isEmpty())
    {
        qWarning() << "[chat] 查在线的 uid 为空，请求被丢弃";
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

    if (sendJson(static_cast<std::uint16_t>(protocol::MessageId::CheckIsUserOnlineReq), body))
    {
        m_online_in_flight = uid;

        /*
         * 查在线是**串行**的（回包不带 uid，没法配对）。这一条超时如果不兜住，
         * 队列就永久卡住 —— 表现出来是"在线状态的小点再也不更新了"，而且完全静默。
         * 这里不发 userOnlineResult：状态仍旧未知，重排队尾再试一次比谎报离线诚实。
         */
        expectReply(static_cast<std::uint16_t>(protocol::MessageId::CheckIsUserOnlineReq),
                    static_cast<std::uint16_t>(protocol::MessageId::CheckIsUserOnlineRsp),
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

            // 与服务端一致：body 超上限按非法报文处理，断开会话（服务端会直接 close）。
            // 注意只对 body_len 校验 —— message_id 是枚举不是长度，拿它和 MAX_BODY_LEN
            // 比毫无意义，且会把 >2047 的新消息号误杀成非法帧。
            if (body_len > protocol::MAX_BODY_LEN)
            {
                qWarning() << "[chat] 非法帧头: message_id =" << message_id
                    << "body_len =" << body_len;
                reset();
                emit linkError(tr("收到的报文不合法，连接已关闭"));
                return;
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
         * 服务端主动推的那两帧（1009 / 1012）不在登记表里，查不到就自然无事发生。
         */
        finishReply(message_id);

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
        default:
            // 其余业务消息按阶段推进逐个接入；未知编号丢弃但不断开连接。
            //
            // 已知的、服务端**尚未实现**的编号（别误判成客户端问题）：
            //   TextChatMsg*      (1013~1015)   服务端完全没有相关实现
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

    bool ChatSocket::sendJson(const std::uint16_t message_id, const QJsonObject& body)
    {
        if (!isConnected())
        {
            qWarning() << "[chat] 未连接，消息被丢弃:" << messageIdName(message_id);
            return false;
        }

        const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
        return sendFrame(message_id, payload);
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
