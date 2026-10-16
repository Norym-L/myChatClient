#include "ChatSocket.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QTcpSocket>
#include <QTimer>

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
        , m_online_timer(new QTimer(this))
    {
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
         * 「查在线」是串行的（回包不带 uid，见 pumpOnlineQueue 的说明）。
         * 串行就得防"回包永远不来"：否则一次丢回包就把整条队列堵死，
         * 表现出来是"在线状态的点点再也不更新了"，而且完全静默。
         * 本地服务端正常是毫秒级，3 秒足够宽松。
         */
        m_online_timer->setSingleShot(true);
        m_online_timer->setInterval(kOnlineReplyTimeoutMs);
        connect(m_online_timer, &QTimer::timeout, this, [this]()
                    {
                        if (m_online_in_flight.isEmpty())
                            return;

                        qWarning() << "[chat] 查询在线状态超时:" << m_online_in_flight;
                        m_online_in_flight.clear();
                        pumpOnlineQueue();
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

        return sendJson(static_cast<std::uint16_t>(protocol::MessageId::ChatLoginReq), body);
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

    return sendJson(static_cast<std::uint16_t>(protocol::MessageId::SearchUserReq), body);
}

bool ChatSocket::sendFriendRequest(const std::uint64_t selfUid, const std::uint64_t toUid,
                                   const QString& verifyInfo, const QString& remark)
{
    QJsonObject body;
    body.insert(QStringLiteral("uid"), QString::number(selfUid));
    body.insert(QStringLiteral("to_uid"), QString::number(toUid));
    body.insert(QStringLiteral("verify_info"), verifyInfo);
    body.insert(QStringLiteral("remark"), remark);

    return sendJson(static_cast<std::uint16_t>(protocol::MessageId::AddFriendReq), body);
}

bool ChatSocket::syncFriendRequests(const std::uint64_t selfUid)
{
    QJsonObject body;
    body.insert(QStringLiteral("uid"), QString::number(selfUid));

    return sendJson(static_cast<std::uint16_t>(protocol::MessageId::SyncFriendRequestReq), body);
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
        m_online_timer->start();
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
        m_online_timer->stop();
        m_buffer.clear();
        m_logged_in = false;
        m_online_in_flight.clear();
        m_online_queue.clear();

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
        m_online_timer->stop();
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
            //   AuthFriendReq/Rsp (1010/1011)   服务端未注册处理器
            //   TextChatMsg*      (1013~1015)   服务端完全没有相关实现
            //   NotifyAddFriendReq(1009)        服务端主动推送，客户端暂未处理
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
        m_online_timer->stop();

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
