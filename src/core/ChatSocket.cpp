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
    {
        m_connect_timer->setSingleShot(true);
        connect(m_connect_timer, &QTimer::timeout, this, [this]()
        {
            qWarning() << "[chat] 连接超时:" << m_host << m_port;
            reset();
            emit connectFailed(tr("连接聊天服务器超时"));
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

void ChatSocket::disconnectFromServer()
{
    reset();
}

void ChatSocket::reset()
    {
        m_connect_timer->stop();
        m_buffer.clear();
        m_logged_in = false;

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
        m_buffer.clear();

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
        const std::uint64_t request_id = id.value_or(0);

        qInfo() << "[chat] 好友申请已发送, request id =" << request_id;
        emit friendRequestResult(true, error, request_id);
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

    bool ChatSocket::sendJson(const std::uint16_t message_id, const QJsonObject& body)
    {
        if (!isConnected())
        {
            qWarning() << "[chat] 未连接，消息被丢弃:" << messageIdName(message_id);
            return false;
        }

        const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
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

        qDebug() << "[chat] 发送" << messageIdName(message_id) << QString::fromUtf8(payload);
        return true;
    }
} // namespace mychat
