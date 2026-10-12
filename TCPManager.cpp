#include "TCPManager.h"

#include <QDataStream>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtEndian>

#include "logger.h"
#include "UserManager.h"

namespace
{
/// 连接 ChatServer 的超时保护（文档：服务端无读超时，客户端必须自己做连接超时）
constexpr int TCP_CONNECT_TIMEOUT_MS = 5000;
} // namespace

TCPManager::TCPManager()
    : m_host(""), m_port(0), m_link_state(LinkState::Idle)
{
    m_connect_timer.setSingleShot(true);
    connect(&m_connect_timer, &QTimer::timeout, this, [this]()
                {
                    if (m_link_state != LinkState::Connecting)
                    {
                        return;
                    }
                    LOG_WARN() << "[tcp] connect timeout:" << m_host << m_port;
                    closeSession("connect timeout");
                    emit sig_connectSuccess(false);
                });

    // 连接完成
    connect(&m_socket, &QTcpSocket::connected, this, [this]()
                {
                    m_connect_timer.stop();
                    m_link_state = LinkState::Connected;
                    m_buffer.clear();
                    LOG_DEBUG() << "[tcp] connected to server" << m_host << m_port;
                    emit sig_connectSuccess(true);
                });

    // 可以读取数据
    connect(&m_socket, &QTcpSocket::readyRead, this, [this]()
                {
                    m_buffer.append(m_socket.readAll());
                    dispatchBuffer();
                });

    // 断开：状态先复位，避免与 errorOccurred 重复上报
    connect(&m_socket, &QAbstractSocket::disconnected, this, [this]()
                {
                    m_connect_timer.stop();
                    const bool be_connected = (m_link_state == LinkState::Connected);
                    const bool be_connecting = (m_link_state == LinkState::Connecting);
                    m_link_state = LinkState::Idle;
                    m_buffer.clear();

                    LOG_DEBUG() << "[tcp] disconnected from server" << m_host << m_port;

                    if (be_connected)
                    {
                        emit sig_tcpError(tr("与聊天服务器的连接已断开"));
                    }
                    else if (be_connecting)
                    {
                        emit sig_connectSuccess(false);
                    }
                });

    // 错误处理
    connect(&m_socket,
            QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
            this,
            [this](QAbstractSocket::SocketError socket_error)
                {
                    Q_UNUSED(socket_error)
                    LOG_WARN() << "[tcp] error:" << m_socket.errorString();

                    if (m_link_state != LinkState::Connecting)
                    {
                        // 已建立连接后的错误由 disconnected 上报
                        return;
                    }

                    closeSession("connect failed: " + m_socket.errorString());
                    emit sig_connectSuccess(false);
                });

    // tcp send data
    connect(this, &TCPManager::sigSendData, this, &TCPManager::slotSendData);

    initHandlers();
}

bool TCPManager::isConnected() const
{
    return m_link_state == LinkState::Connected && m_socket.state() == QAbstractSocket::ConnectedState;
}

void TCPManager::closeSession(const QString& reason)
{
    if (m_link_state != LinkState::Idle)
    {
        LOG_DEBUG() << "[tcp] close session:" << reason;
    }

    m_connect_timer.stop();
    m_buffer.clear();
    m_link_state = LinkState::Idle;
    m_socket.abort();
}

QJsonObject TCPManager::ParseToJson(const QByteArray& body)
{
    QJsonParseError parse_error{};
    // 文档：body 为多行 JSON，非 ASCII 字符可能被转义为 \uXXXX，按标准 JSON 解析
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        LOG_WARN() << "[tcp] resp json parse failed:" << parse_error.errorString() << body;
        QJsonObject json;
        json["error"] = ErrorCodes::ERROR_JSON;
        return json;
    }

    const QJsonObject json_object = document.object();
    return json_object;
}

void TCPManager::initHandlers()
{
    m_handlers.emplace(
        ReqId::ID_CHAT_LOGIN_RSP,
        [this](const QByteArray& body)
            {
                const auto json_object = ParseToJson(body);
                // 文档：失败响应仅含 error 字段（1011 uid非法 / 1012 token不一致 / 1002 RPC失败）
                const int error = json_object.value("error").toInt(ErrorCodes::ERROR_JSON);
                if (error != ErrorCodes::SUCCESS)
                {
                    LOG_WARN() << "[tcp] login to chat server failed, error:" << error;
                    emit sig_loginFailed(error);
                    return;
                }

                // uid 由服务端以 uint64 字符串形式下发（QJson 数字只有 53 位有效整数）。
                // 原实现直接用 toString() + std::stoull：服务端若按旧约定发数字，
                // toString() 得到空串，std::stoull 会抛 std::invalid_argument 导致进程终止
                const std::optional<std::uint64_t> uid = parseUid(json_object.value("uid"));
                if (!uid.has_value())
                {
                    LOG_WARN() << "[tcp] login resp has invalid uid:" << json_object;
                    emit sig_loginFailed(ErrorCodes::ERR_JSON);
                    return;
                }

                const QString name = json_object.value("name").toString();
                const QString token = json_object.value("token").toString();

                auto& user = UserManager::GetInstance();
                user.setUid(uid.value());
                user.setName(name);
                user.setToken(token);

                LOG_INFO() << "[tcp] login to chat server success, uid:" << uid.value() << "name:" << name;
                emit sig_switchToChatDialog();
            });
    m_handlers.emplace(
        ReqId::ID_SEARCH_USER_RSP,
        [this](const QByteArray& body)
            {
                const auto json_object = ParseToJson(body);
                // 文档：失败响应仅含 error 字段（1011 uid非法 / 1012 token不一致 / 1002 RPC失败）
                const int error = json_object.value("error").toInt(ErrorCodes::ERROR_JSON);
                if (error != ErrorCodes::SUCCESS && error != ErrorCodes::USER_NOT_FOUND)
                {
                    LOG_WARN() << "[tcp] search user failed, error:" << error;
                    emit sigUserSearchFinish(nullptr);
                    return;
                }

                // uid 由服务端以 uint64 字符串形式下发；返回 0 表示未找到
                const std::optional<std::uint64_t> uid = parseUid(json_object.value("uid"));
                if (!uid.has_value())
                {
                    LOG_WARN() << "[tcp] search resp has invalid uid:" << json_object;
                    emit sigUserSearchFinish(nullptr);
                    return;
                }

                if (uid.value() == 0 || error == ErrorCodes::USER_NOT_FOUND)
                {
                    LOG_DEBUG() << "[tcp] search user not found";
                    emit sigUserSearchFinish(nullptr);
                    return;
                }

                auto serch_info = std::make_shared<SearchInfo>(
                    uid.value(),
                    json_object.value("user_name").toString(),
                    json_object.value("desc").toString(),
                    static_cast<Sex>(json_object.value("sex").toInt()),
                    json_object.value("icon").toString()
                );

                emit sigUserSearchFinish(std::move(serch_info));
            });
    // 收到好友申请
    m_handlers.emplace(
        ReqId::ID_AUTH_FRIEND_REQ,
        [this](const QByteArray& body)
            {
                const auto json_object = ParseToJson(body);
                const int error = json_object.value("error").toInt(ErrorCodes::ERROR_JSON);
                if (error != ErrorCodes::SUCCESS)
                {
                    LOG_WARN() << "[tcp] receive a auth friend request failed, error:" << error;
                    return;
                }

                const auto uid = parseUid(json_object.value("uid"));
                if (!uid.has_value())
                {
                    LOG_WARN() << "[tcp] receive a auth friend request has invalid uid:" << json_object;
                    return;
                }
                auto auth_friend_info = std::make_shared<AddFriendApply>(
                    uid.value(),
                    json_object["name"].toString(),
                    json_object["verify_info"].toString(),
                    json_object["icon"].toString()
                );
#ifdef _DEBUG
                LOG_DEBUG() << "recv add friend request as <> id: "
                    << uid
                    << "name:" << json_object["name"].toString()
                    << "verify_info: " << json_object["verify_info"].toString()
                    << "icon: " << json_object["icon"].toString();
#endif
                sigReciveFriendApply(std::move(auth_friend_info));
            }
    );
}

void TCPManager::slot_tcpConnect(ServerInfo server_info)
{
    if (server_info._host.isEmpty() || server_info._port.isEmpty())
    {
        LOG_WARN() << "[tcp] invalid chat server info, host:" << server_info._host
            << "port:" << server_info._port;
        emit sig_connectSuccess(false);
        return;
    }

    bool be_valid_port = false;
    const quint16 port = server_info._port.toUShort(&be_valid_port);
    if (!be_valid_port || port == 0)
    {
        LOG_WARN() << "[tcp] invalid chat server port:" << server_info._port;
        emit sig_connectSuccess(false);
        return;
    }

    closeSession("re-connect");

    m_host = server_info._host;
    m_port = port;
    m_link_state = LinkState::Connecting;

    LOG_DEBUG() << "[tcp] connecting to server" << m_host << m_port;
    m_connect_timer.start(TCP_CONNECT_TIMEOUT_MS);
    m_socket.connectToHost(m_host, m_port);
    (void)std::move(server_info);
}

// NOLINTNEXTLINE == data 异步执行需要复制
void TCPManager::slotSendData(const ReqId req_id, QByteArray data)
{
    const std::optional<std::uint16_t> message_id = ChatProtocol::toMessageId(req_id);
    if (!message_id.has_value())
    {
        LOG_WARN() << "[tcp] no wire message id for req id:" << static_cast<int>(req_id);
        return;
    }

    if (!isConnected())
    {
        LOG_WARN() << "[tcp] socket is not connected, message dropped";
        return;
    }

    const quint16 len = static_cast<quint16>(data.size());

    QByteArray block;
    QDataStream out(&block, QIODevice::WriteOnly);

    out.setByteOrder(QDataStream::BigEndian);

    out << message_id.value() << len;

    block.append(data);
    m_socket.write(block);
}

bool TCPManager::sendJsonMessage(const ReqId req_id, const QJsonObject& body)
{
    const std::optional<std::uint16_t> message_id = ChatProtocol::toMessageId(req_id);
    if (!message_id.has_value())
    {
        LOG_WARN() << "[tcp] no wire message id for req id:" << static_cast<int>(req_id);
        return false;
    }

    if (!isConnected())
    {
        LOG_WARN() << "[tcp] socket is not connected, message dropped";
        return false;
    }

    // 文档：响应体为 JsonCpp toStyledString() 的多行 JSON，客户端发送使用紧凑 JSON 即可
    const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
    if (payload.size() > ChatProtocol::MAX_LENGTH)
    {
        LOG_WARN() << "[tcp] body is too long:" << payload.size();
        return false;
    }

    QByteArray frame;
    frame.reserve(ChatProtocol::HEAD_TOTAL_LEN + payload.size());

    QDataStream out(&frame, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::BigEndian); // 网络字节序：message_id、body_len 均为大端
    out << static_cast<std::uint16_t>(message_id.value())
        << static_cast<std::uint16_t>(payload.size());
    frame.append(payload);

    if (m_socket.write(frame) != frame.size())
    {
        LOG_WARN() << "[tcp] write failed:" << m_socket.errorString();
        return false;
    }
    m_socket.flush();

    LOG_DEBUG() << "[tcp] send message_id:" << message_id.value() << "body:" << payload;
    return true;
}

void TCPManager::dispatchBuffer()
{
    for (;;)
    {
        if (m_buffer.size() < ChatProtocol::HEAD_TOTAL_LEN)
        {
            return; // 头部不完整，继续等待后续数据
        }

        const auto* head = reinterpret_cast<const uchar*>(m_buffer.constData());
        const std::uint16_t message_id = qFromBigEndian<std::uint16_t>(head);
        const std::uint16_t body_len = qFromBigEndian<std::uint16_t>(head + ChatProtocol::HEAD_ID_LEN);

        // 文档：message_id 或 body_len 超过 MAX_LENGTH 时为非法报文，服务端会关闭会话
        if (message_id > ChatProtocol::MAX_LENGTH || body_len > ChatProtocol::MAX_LENGTH)
        {
            closeSession(QString("illegal frame head, message_id=%1 body_len=%2")
                         .arg(message_id).arg(body_len));
            emit sig_tcpError(tr("收到的报文不合法，连接已关闭"));
            return;
        }

        const int frame_len = ChatProtocol::HEAD_TOTAL_LEN + body_len;
        if (m_buffer.size() < frame_len)
        {
            return; // body 未收全，继续等待后续数据
        }

        const QByteArray body = m_buffer.mid(ChatProtocol::HEAD_TOTAL_LEN, body_len);
        m_buffer.remove(0, frame_len);

        LOG_DEBUG() << "[tcp] recv message_id:" << message_id << "body:" << body;
        handleMessage(message_id, body);
    }
}

void TCPManager::handleMessage(const std::uint16_t message_id, const QByteArray& body)
{
    const std::optional<ReqId> req_id = ChatProtocol::toReqId(message_id);
    if (!req_id.has_value())
    {
        // 文档：未知 message_id 记录告警并丢弃该消息，不断开连接
        LOG_WARN() << "[tcp] unknown message_id, message dropped:" << message_id;
        return;
    }

    const auto iter = m_handlers.find(req_id.value());
    if (iter == m_handlers.end())
    {
        LOG_WARN() << "[tcp] no handler for req id:" << static_cast<int>(req_id.value());
        return;
    }

    iter->second(body);
}
