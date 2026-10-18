#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <cstdint>
#include <functional>
#include <optional>

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
 *   - **出站** body 超过 MAX_BODY_LEN(2048) 服务端会关会话（发送前自查）
 *   - **入站不做长度校验**：服务端发帧时并不校验长度，好友列表这类回包可能超过
 *     2048；body_len 的物理上限是 65535（2 字节字段）。详见 dispatchBuffer()。
 *
 * ⚠ **每一条"发出去等回包"的请求都登记在线索表 \l m_pending 里**，由同一个
 *   \l m_reply_timer 兜超时（\l kReplyTimeoutMs）。原因不是洁癖：服务端存在
 *   "某个分支直接 return、一句都不回"的实现（实测：拿不属于自己的申请去发 1010
 *   就是这种），而 doctcp 层面**不存在任何"一定会有回包"的保证**。没有这道统一闸门，
 *   任何一条链路卡住都会表现成界面永远转圈、后续请求全被"上一个还没回包"挡掉。
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

        /**
         * 拉取「别人发给我的」好友申请列表（message_id = SyncFriendRequestReq）
         */
        bool syncFriendRequests(std::uint64_t selfUid);

        /**
         * 接受 / 拒绝别人发来的好友申请（message_id = AuthFriendReq 1010）。
         *
         * @param requestId 申请编号（来自 1008 列表或 1009 推送）
         * @param state     1 = 接受，2 = 拒绝（服务端只认这两个）
         *
         * 结果走 \l authFriendRequestResult；回包会回显申请编号（`state` 不回显），
         * 详见 ServerDocs/api-friend.md §5。
         */
        bool authFriendRequest(std::uint64_t requestId, int state);

        /**
         * 拉取好友列表（message_id = SyncFriendListReq 1020）。
         *
         * 服务端**不解析 body**（用户取 `session->getUserId()`），所以发空 JSON `{}` 即可 ——
         * 不要像 1007 那样把 selfUid 塞进去，服务端根本不看，反而容易让人误以为
         * "传谁就查谁"。未登录时服务端回 1011。
         *
         * 结果走 \l friendListResult。
         */
        bool syncFriendList();

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

        /**
         * 发送一条单聊消息（message_id = TextChatMsgReq 1013）。
         *
         * 请求体：`{ "msg_id": "...", "to_uid": "...", "type": "1", "body": "..." }`
         * （四个字段服务端都按**字符串**读；`from_uid` 由服务端从会话里取）。
         * 回执 1014 会把 `msg_id` **原样回显**（成功、失败都带）——
         * 这是把回执对回本地气泡的唯一可靠依据。
         *
         * @param msgId 客户端生成的消息标识，**必填**。服务端不解析、只回显；
         *              缺失时回显空串，那条回执就再也认不回来了。
         *
         * ⚠ 内部仍然**串行发送**：上一条没回包之前，后续消息先排队。
         *   原因不在业务层（业务层已经有 msg_id），而在 \l m_pending 超时表 ——
         *   它按回包型号 FIFO 认领，并发时先回的那条会把别人的账销掉。
         *   一次一条就永远不会错配，代价只是几条消息各等一个 RTT（本机毫秒级）。
         *
         * @return true  = 已进入发送队列（结果看 textMessageResult；成功也只代表
         *                **已写入服务端离线消息流**，不代表对端收到）
         *         false = 根本没发（未登录 / 未连接 / 参数非法 / msgId 为空 / body 超长）
         */
        bool sendTextMessage(const QString& msgId, const QString& toUid, const QString& body,
                             protocol::ChatMessageType type = protocol::ChatMessageType::Text);

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

        /**
         * 好友列表（1020/1021）。
         *
         * 空列表是**正常结果**（还没有好友），不是错误 —— 判断失败要看 error。
         * 超时也会走到这里（error = \l RequestTimeout），否则调用方会一直等。
         */
        void friendListResult(const QList<mychat::protocol::FriendInfo>& friends, int error);

        /**
         * 服务端**主动推送**「有人申请加你为好友」（message_id = 1009 NotifyAddFriendReq）。
         *
         * 只有 `request_id` —— 推送体里不带对方 uid / 昵称 / 验证信息，
         * 所以收到之后必须再发一次 SyncFriendRequestReq 才能拿到详情。
         *
         * 服务端只推给**在线**用户（Redis `uip_<uid>` 未过期），离线期间发的申请
         * 不会有补推，只能靠登录后的第一次 refreshApplyList()。
         */
        void friendRequestNotified(std::uint64_t requestId);

        /**
         * 处理好友申请（1010）的回包（1011）。
         *
         * \note 回包从 2026-10-17 起会**原样回显** `add_friend_request_id`（成功失败都带），
         * 所以 requestId 优先取回包里的值；老服务端不回显时退回"请求侧记下的那一发"。
         * `state` 始终不回显 —— 没有在飞请求时 **state 为 0 表示"未知"**，
         * 调用方此时应该去重新拉列表，而不是猜是接受还是拒绝。
         */
        void authFriendRequestResult(bool ok, int error, std::uint64_t requestId, int state);

        /**
         * 服务端**主动推送**：我发出的申请被对方处理了（message_id = 1012
         * NotifyAuthFriendReq）。
         *
         * @param requestId 申请编号（推送里是**数字**，不是字符串）
         * @param state     1 = 对方接受，2 = 对方拒绝
         *
         * 只推给**在线**的申请人；离线期间的处理结果不会补推，只能靠下次拉列表看到。
         */
        void friendRequestHandled(std::uint64_t requestId, int state);

        /// 一次心跳的结果（error 非 0 说明续期失败，比如 Redis 挂了）
        void heartbeatResult(bool ok, int error);
        /// 某个用户的在线状态；error 非 0 时 online 无意义
        void userOnlineResult(std::uint64_t uid, bool online, int error);

        /**
         * 一条消息的最终投递结果（1013 → 1014，或者客户端本地超时）。
         *
         * 服务端在 1014 里把请求的 `msg_id` **原样回显**，所以这里以它为准，
         * 调用方按 msgId 精确认领即可 —— **不需要**再依赖发送顺序。
         * 顺带把请求里的 \a toUid / \a body 带回来，方便日志和兜底。
         *
         * @param ok    服务端 error == 0（含义是"已写入对方离线消息流"，不是"对方已读"）
         * @param error 失败原因（服务端码或本地的 \l ErrorCode::RequestTimeout）
         */
        void textMessageResult(bool ok, int error, const QString& msgId,
                               const QString& toUid, const QString& body);

        /**
         * 服务端推送「你有新消息」（message_id = 1022 NotifyReceiveMessage）。
         *
         * ⚠ 推送体只有 `{"error":0}`：**没有发送者、没有类型、没有内容**，
         *   服务端也**没有**消息拉取 / 历史接口，所以收到之后拿不到消息本身 ——
         *   客户端目前只能提示"有新消息"。等拉取接口（提案 1023/1024）落地再补内容。
         */
        void messageReceived();

    private
        slots :

        void onConnected();
        void onDisconnected();
        void onReadyRead();
        void onSocketError(QAbstractSocket::SocketError error);

    private:
        explicit ChatSocket(QObject* parent = nullptr);

        /*
         * ── 统一的回包超时机制 ──────────────────────────────────────────
         *
         * 服务端没有读超时，也不保证每帧请求都有回包，所以客户端必须自己记账：
         * 发出去一个请求就在 \l m_pending 里挂一条，回包到了由 finishReply() 摘掉，
         * 到期还没回就按「网络连接超时」把它报出去。
         */
        struct PendingReply
        {
            /// 请求帧的 message_id（日志用）
            std::uint16_t request_id = 0;
            /// 期待的回包 message_id
            std::uint16_t reply_id = 0;
            /// 到期时刻（\l m_clock 上的毫秒）
            qint64 deadline_ms = 0;
            /// 超时后要做的收尾：通常是 emit 一条"失败"信号，把调用方从 busy 里放出来
            std::function<void()> on_timeout;
        };

        /**
         * 登记「我刚发了 requestId，等 replyId」。
         *
         * @param onTimeout 到期回调。**必须负责把调用方的等待状态收干净**
         *                 （m_auth_in_flight 之类的标记由回调自己清）。
         */
        void expectReply(std::uint16_t requestId, std::uint16_t replyId,
                         std::function<void()> onTimeout);
        /**
         * 某个回包到了：把登记表里**最早**的那条同型号清掉，并重排定时器。
         *
         * \note 由 handleFrame 在最前面统一调用，各 handler 不用记得喊它 ——
         *       那就是"每加一条新消息都要记得加一行"的典型漏法。
         */
        void finishReply(std::uint16_t replyId);
        /// 按最早到期的那条重设单次定时器（表空就停）
        void armReplyTimer();
        /// 全部作废：断线 / 复位时调用，不再补报超时（连接都没了，报了也没意义）
        void clearPendingReplies();

        /// 关闭并复位会话；silent 为 true 时不发任何信号
        void reset();
        /// 按帧头拆包：一次可能收到多帧或半帧
        void dispatchBuffer();
        void handleFrame(std::uint16_t message_id, const QByteArray& body);
        void handleLoginResponse(const QByteArray& body);
        void handleSearchUserResponse(const QByteArray& body);
        void handleAddFriendResponse(const QByteArray& body);
        void handleFriendRequestList(const QByteArray& body);
        /// 好友列表回包（1021）
        void handleFriendList(const QByteArray& body);
        /// 服务端主动推送：只有 request id，没有回包可发
        void handleNotifyAddFriendRequest(const QByteArray& body);
        /// 处理好友申请的回包（1011，只有 error）
        void handleAuthFriendResponse(const QByteArray& body);
        /// 我发出的申请被处理了（1012 推送）
        void handleNotifyAuthFriend(const QByteArray& body);
        void handleHeartbeatResponse(const QByteArray& body);
        /// 回包**不带 uid**，所以靠 m_online_in_flight 记住这一发问的是谁
        void handleUserOnlineResponse(const QByteArray& body);
        /// 消息发送回执（1014，只有 error，靠 m_text_in_flight 认领）
        void handleTextMessageResponse(const QByteArray& body);
        /// 服务端主动推送「你有新消息」（1022，不含内容）
        void handleNotifyReceiveMessage(const QByteArray& body);
        bool sendJson(std::uint16_t message_id, const QJsonObject& body);
        /// 直接发一段已序列化好的 body（心跳要发体长 0 的空帧，走不了 sendJson）
        bool sendFrame(std::uint16_t message_id, const QByteArray& payload);
        /// 一次只发一个「查在线」请求；上一个回包了就接着发下一个
        void pumpOnlineQueue();
        /// 一次只发一条聊天消息；上一条回包（或超时）了就接着发下一条
        void pumpTextQueue();

        /// 文档：服务端无读超时，客户端自己做连接超时
        static constexpr int kConnectTimeoutMs = 5000;
        /**
         * **所有**请求共用的回包超时窗口。
         *
         * 本地服务端正常是毫秒级，5 秒足够宽松；真超时了通常不是网络慢，
         * 而是服务端那条分支压根不回包 —— 所以报的是 \l RequestTimeout，
         * 不是 \l NetworkError。
         */
        static constexpr int kReplyTimeoutMs = 5000;

        QTcpSocket* m_socket = nullptr;
        QTimer* m_connect_timer = nullptr;
        QTimer* m_heartbeat_timer = nullptr;
        /// 唯一负责"回包等太久"的那只表；所有链路共用
        QTimer* m_reply_timer = nullptr;
        /// 给 deadline 计时用（单调，不受系统改时间影响）
        QElapsedTimer m_clock;

        /// 当前在飞、还没等到回包的请求（FIFO）
        QList<PendingReply> m_pending;

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

        /*
         * 「处理好友申请」的回包（1011）只带 error，不带申请编号，
         * 所以这次请求的 id / state 必须先记下来，回包时再拼回去。
         * 同一时刻只允许一个在处理中（界面一次只能点一个按钮）。
         */
        std::uint64_t m_auth_in_flight = 0;
        int m_auth_state_in_flight = 0;

        /// 一条待发的聊天消息（1013）
        struct OutgoingText
        {
            /// 客户端生成的消息标识，服务端原样回显
            QString msg_id;
            QString to_uid;
            QString body;
            int type = 1;
        };

        /*
         * 消息发送**串行**：`m_pending` 的超时表按"请求型号"FIFO 认领，
         * 并发发多条时，哪一条先回包就会把最早那笔账销掉 —— 一旦有分支不回包，
         * 状态就张冠李戴。串行一次只压一条，配合 1014 回显的 msg_id，
         * 业务层可以精确认领，队列层也永远不会错配。
         */
        QList<OutgoingText> m_text_queue;
        /// 当前在飞的那一条；没有在飞时为空
        std::optional<OutgoingText> m_text_in_flight;
    };
} // namespace mychat
