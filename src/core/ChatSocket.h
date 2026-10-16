#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

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

        /**
         * 心跳（message_id = HeartBeatReq）。
         *
         * 登录成功后由内部定时器按 HEARTBEAT_INTERVAL_MS 自动发送，外部一般不用调。
         * 服务端只是拿它给 Redis 里的 `uip_<uid>` 续期（30 秒 TTL）—— 30 秒没续上，
         * 别人查你就是 offline。**服务端不校验 body**，但这里仍发一个空 JSON 对象
         * 而不是 0 长度 body：服务端的拆帧在 body_len == 0 时会走到
         * `async_read_some(零长缓冲)` 那条没被验证过的路径（asio 可能当成 EOF 直接关会话）。
         */
        bool sendHeartbeat();

        /**
         * 查询某个用户是否在线（message_id = CheckIsUserOnlineReq）。
         *
         * @param uid 目标 uid（服务端要求**字符串**形式，和别处一致）
         * 结果走 \l userOnlineResult 信号
         */
        bool checkUserOnline(const QString& uid);

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

        /// 一次心跳的结果（error 非 0 说明续期失败，比如 Redis 挂了）
        void heartbeatResult(bool ok, int error);
        /// 某个用户的在线状态；error 非 0 时 online 无意义
        void userOnlineResult(std::uint64_t uid, bool online, int error);

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
        void handleHeartbeatResponse(const QByteArray& body);
        /// 回包**不带 uid**，所以靠 m_online_in_flight 记住这一发问的是谁
        void handleUserOnlineResponse(const QByteArray& body);
        bool sendJson(std::uint16_t message_id, const QJsonObject& body);
        /// 直接发一段已序列化好的 body（心跳要发体长 0 的空帧，走不了 sendJson）
        bool sendFrame(std::uint16_t message_id, const QByteArray& payload);
        /// 一次只发一个「查在线」请求；上一个回包了就接着发下一个
        void pumpOnlineQueue();

        /// 文档：服务端无读超时，客户端自己做连接超时
        static constexpr int kConnectTimeoutMs = 5000;
        /// 「查在线」回包超时：串行队列靠它自愈（回包不来时不能把后面的全堵死）
        static constexpr int kOnlineReplyTimeoutMs = 3000;

        QTcpSocket* m_socket = nullptr;
        QTimer* m_connect_timer = nullptr;
        QTimer* m_heartbeat_timer = nullptr;
        QTimer* m_online_timer = nullptr;

        QByteArray m_buffer;
        QString m_host;
        quint16 m_port = 0;
        bool m_logged_in = false;

        /*
         * 「查在线」回包里只有 error / online，**没有 uid** —— 想同时查多个人，
         * 回包就没法和请求对上号。
         *
         * 服务端只有一个 dealMessage 处理线程、每次请求必回一包，所以理论上可以按 FIFO
         * 配对；但那是在赌"一请求一回包"永远成立，一旦哪天有分支不回包，
         * 在线状态就会张冠李戴（而且错得很安静）。
         *
         * 所以这里选择**串行**：一次只发一个，回来了再发下一个。少一点并发，
         * 换来不依赖任何顺序假设。要真正并发，得先请服务端把 uid 回显回来。
         */
        QString m_online_in_flight;
        QStringList m_online_queue;
    };
} // namespace mychat
