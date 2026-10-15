#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include <cstdint>

#include "protocol.h"

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

        /**
         * 搜索用户（message_id = SearchUserReq）。
         *
         * @param keyword 服务端先按纯数字当 uid 查，查不到再按用户名查；
         *                所以这里**原样传字符串**，不要自己判断它是不是数字。
         */
        bool searchUser(const QString& keyword);

        /**
         * 发送好友申请（message_id = AddFriendReq）。
         *
         * 服务端字段顺序在 接口文档.md 里是 uid / verify_info / remark / to_uid；
         * 这里显式把 to_uid 也传上（服务端读的是 to_uid）。
         */
        bool sendFriendRequest(std::uint64_t selfUid, std::uint64_t toUid,
                               const QString& verifyInfo, const QString& remark);

        /// 拉取「别人发给我的」好友申请列表（message_id = SyncFriendRequestReq）
        bool syncFriendRequests(std::uint64_t selfUid);

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

        /// 搜索用户结果；found=false 表示没找到或出错（看 error）。
        /// 服务端可返回多个命中（同名用户），infos 为空即未找到。
        void searchUserResult(bool found, const QList<mychat::protocol::UserInfo>& infos, int error);
        /// 发送好友申请的结果
        void friendRequestResult(bool ok, int error, std::uint64_t requestId);
        /// 好友申请列表
        void friendRequestList(const QList<mychat::protocol::FriendRequest>& requests, int error);

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
        void handleSearchUserResponse(const QByteArray& body);
        void handleAddFriendResponse(const QByteArray& body);
        void handleFriendRequestList(const QByteArray& body);
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
