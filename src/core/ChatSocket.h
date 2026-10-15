#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QObject>
#include <QString>

#include <cstdint>

class QJsonObject;
class QTcpSocket;
class QTimer;

namespace mychat
{
    /**
 * 与 ChatServer 的裸 TCP 连接（帧格式见 protocol.h）。
 *
 * 典型用法：
 *   connectToServer(host, port)  ->  connected() / connectFailed(reason)
 *   login(uid, token)            ->  loginSucceeded(uid, name, token) / loginFailed(error)
 *
 * 服务端限制（见 docs/ServerDocs/api-tcp.md §3）：
 *   - 无读超时，客户端必须自己做连接超时（这里用 QTimer 保护）
 *   - message_id 或 body_len 超过 MAX_BODY_LEN 视为非法报文，服务端会关闭会话
 */
    class ChatSocket : public QObject
    {
        Q_OBJECT

    public:
        static ChatSocket& instance();

        /// 连接指定的 ChatServer
        void connectToServer(const QString& host, std::uint16_t port);
        /// 发送登录帧（message_id = ChatLoginReq）；返回 false 表示未连接或 body 过长
        bool login(std::uint64_t uid, const QString& token);
        /// 断开并复位全部状态
        void disconnectFromServer();

        [[nodiscard]] bool isConnected() const;
        [[nodiscard]] bool isLoggedIn() const { return m_logged_in; }

        signals :

        void connected();
        void connectFailed(const QString& reason);
        void loginSucceeded(std::uint64_t uid, const QString& name, const QString& token);
        void loginFailed(int error);
        /// 连接建立之后的链路异常（已断开 / 报文非法）
        void linkError(const QString& reason);

    private
        slots :

        void onConnected();
        void onDisconnected();
        void onReadyRead();
        void onSocketError(QAbstractSocket::SocketError error);

    private:
        explicit ChatSocket(QObject* parent = nullptr);

        /// 关闭并复位会话；silent 为 true 时不发任何信号
        void reset();
        /// 按帧头拆包：一次可能收到多帧或半帧
        void dispatchBuffer();
        void handleFrame(std::uint16_t message_id, const QByteArray& body);
        void handleLoginResponse(const QByteArray& body);
        bool sendJson(std::uint16_t message_id, const QJsonObject& body);

        /// 文档：服务端无读超时，客户端自己做连接超时
        static constexpr int kConnectTimeoutMs = 5000;

        QTcpSocket* m_socket = nullptr;
        QTimer* m_connect_timer = nullptr;

        QByteArray m_buffer;
        QString m_host;
        quint16 m_port = 0;
        bool m_logged_in = false;
    };
} // namespace mychat
