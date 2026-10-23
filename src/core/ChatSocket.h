#pragma once

#include <QAbstractSocket>
#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
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
         * 删除好友（message_id = DelFriendReq 1027）。
         *
         * 请求体 `{ "uid": "<好友 uid>" }`，删除者取**当前会话已登录的 uid**
         * （所以不用传自己）。服务端做双向删除，并同步 Redis 好友集合。
         *
         * ⚠ 服务端**不通知被删除方**（1028 只回给发起方）—— 对方的会话列表要等
         *   他自己刷新好友列表（1020）才会变。
         *
         * @return true = 已发出（结果看 \l friendDeleteResult）
         *         false = 没发（未连接 / 未登录 / uid 为空）
         */
        bool delFriend(const QString& uid);

        /**
         * 设置好友备注（message_id = SetFriendRemarkReq 1029）。
         *
         * 请求体 `{ "id": "<本地编号>", "friend_uid": "<好友 uid>", "remark": "<备注>" }`。
         * 服务端把 `id` **原样回显**（不解析），备注是**单向**的：只改「我给对方起的名字」，
         * 对方无感知。`remark` 传空串即清空备注。
         *
         * ⚠ 服务端成功后**不会主动推送**，好友列表要自己刷（1020）才能看到新备注；
         *   本方法把请求侧的 uid / remark 一路带回信号，控制器可以就地更新列表。
         *
         * @return true  = 已发出（结果看 \l friendRemarkResult）
         *         false = 没发（未连接 / 参数非法 / 上一个备注请求还没回包）
         */
        bool setFriendRemark(const QString& friendUid, const QString& remark);

        /**
         * 编辑本人资料（message_id = ChangeUserInfoReq 1031）。
         *
         * 请求体 `{ "id": "...", "name": "...", "desc": "...", "sex": 0|1|2 }`。
         * 协议里**所有字段都可选**（缺失 = 不修改）；这里按"编辑表单一次提交"的语义
         * 三项都发 —— 想只改签名就把原昵称一起带上，否则会被当成空串写进库。
         *
         * `icon`（头像）故意不发：服务端没有头像上传通道，发了只会把已有地址覆盖成空。
         *
         * @return true  = 已发出（结果看 \l userInfoChangeResult）
         *         false = 没发（未连接 / name 为空 / 上一个资料请求还没回包）
         */
        bool changeUserInfo(const QString& name, const QString& desc, int sex);

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
         * 发送一条消息（message_id = TextChatMsgReq 1013）。
         *
         * 请求体：`{ "msg_id": "...", "to_uid": "...", "type": "1", "body": "..." }`
         * （四个字段服务端都按**字符串**读；`from_uid` 由服务端从会话里取）。
         * 回执 1014 会把 `msg_id` **原样回显**（成功、失败都带）——
         * 这是把回执对回本地气泡的唯一可靠依据。
         *
         * @param msgId 客户端生成的消息标识，**必填**。服务端不解析、只回显；
         *              缺失时回显空串，那条回执就再也认不回来了。
         * @param group 群消息标记（2026-10-23 起的 `group` 字段）。
         *              true 时 `toUid` 传的是**群 id**，服务端改校验
         *              `SISMEMBER chatgroup_<gid> <自己>`，不再校验好友。
         *              ⚠ 必须发 JSON **布尔** —— 服务端用 `asBool()`，
         *                传字符串 "true"/"1" 也能过，但传 `"false"` 同样为真，
         *                以后想改回单聊就会踩雷。
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
                             protocol::ChatMessageType type = protocol::ChatMessageType::Text,
                             bool group = false);

        /**
         * 拉取消息（message_id = SyncChatMessageReq 1023）。
         *
         * 请求体只有 `{ "count": "<string>" }`，拉取对象就是当前会话的登录 uid。
         * 服务端**单次最多 10 条**，`count > 10` 直接回 1020 —— 这里会先夹到 1..10。
         * 未登录时服务端回 `1011`（2026-10-18 起所有 handler 都查登录态）。
         *
         * 结果走 \l chatMessagesReceived。**服务端按 PEL 优先**：已投递但未确认的消息
         * 会在下一次拉取（包括重连后）**重复返回**（at-least-once）——
         * 所以调用方必须按 `msgs[].id` 去重，拿到之后仍要用 \l ackChatMessages 确认。
         *
         * @return false = 没发（未登录 / 未连接 / 已经有一个拉取在飞）
         */
        bool syncChatMessages(int count = protocol::MAX_CHAT_MESSAGES_PER_BATCH);

        /**
         * 确认收到（message_id = AckChatMessageReq 1025）。
         *
         * @param ids 来自 1024 的 `msgs[].id`，**最多 10 个**（服务端硬上限）。
         * 服务端执行 `XACK + XDEL` —— **确认后消息即被删除，不能再次拉取**。
         */
        bool ackChatMessages(const QStringList& ids);

        // ══════════════════════════════════════════════════════════════
        //  群聊管理（2026-10-23 服务端 1033–1042）
        //
        //  这一组接口有两个共同的协议特点，写在这里一次说清：
        //    · 回包**只有 error**（1034 例外，带 group_id）—— 群 id / 成员
        //      只能靠请求侧记下来再拼回信号，和 1029/1031 是同一种处理；
        //    · 服务端**不校验**权限 / 群是否存在，全部交给存储过程，
        //      返回负值再映射成 1023/1024/1013（见 api-group.md §4–§8）。
        //
        //  ⚠ 回包**不带群 id**，所以"这一发是给哪个群"必须靠请求侧记下来
        //     （\l m_group_requests，按 msgid 存）。2026-10-22 起服务端回显
        //     msgid，因此允许多条群请求并发；但界面本来就一次点一个按钮，
        //     真正的并发只会来自"建群刚回 + 用户立刻打开成员面板"这种时序。
        // ══════════════════════════════════════════════════════════════

        /**
         * 创建群聊（1033）。
         *
         * 请求体 `{ "name": "...", "avator": "...", "announcement": "..." }`。
         * `avator` 的**拼写就是错的**，服务端只认它，发 `avatar` 会被静默忽略。
         *
         * 群主固定是当前会话 uid（请求里不传）；`max_members` 服务端硬编码 500。
         * 新群**只有群主一个人** —— 初始成员要另发 \l addGroupMembers。
         * 成功后服务端会给群主写一条系统消息「群聊 &lt;gid&gt; 已创建」并推 1022。
         *
         * @return true = 已发出（结果看 \l groupCreated，成功时带回新群 id）
         */
        bool createGroup(const QString& name, const QString& avatar, const QString& announcement);

        /**
         * 拉人入群（1035）。
         *
         * 请求体 `{ "group_id": "...", "members": ["10002", ...] }`。
         * 服务端逐个调存储过程，**没有事务**：中途失败时前面的成员已经进库了。
         * 已经在群里的成员会被跳过（存储过程 -3），不算失败。
         * 成功后给**所有成员（含新成员）**写系统消息「邀请了 N 个好友」并推 1022。
         *
         * ⚠ 如果本次**一个都没加进去**（全在群里 / 空数组），服务端仍会用空数组
         *   调 `SADD`，Redis 报错后异常逃逸 → 回包**只有 msgid、没有 error**。
         *   这种回包在客户端表现为超时（5s 后 \l RequestTimeout），不是成功。
         *
         * @return true = 已发出（结果看 \l groupMembersAdded）
         */
        bool addGroupMembers(const QString& gid, const QStringList& uids);

        /**
         * 踢人（1037）。
         *
         * 请求体 `{ "group_id": "...", "uid": "<被踢者>" }`。
         * 只有群主 / 管理员能踢；不能踢群主、管理员之间不能互踢。
         * 成功时服务端**先**给所有成员（含被踢者）写系统消息「踢出了 &lt;uid&gt;」
         * 并推 1022，**再** SREM；被踢者离线流里的历史群消息不清理。
         *
         * 回包 `1014 USER_NOT_FOUND` 在这里表示"目标用户不在群内"。
         *
         * @return true = 已发出（结果看 \l groupMemberKicked）
         */
        bool kickGroupMember(const QString& gid, const QString& uid);

        /**
         * 解散群聊（1039）。只有群主可以。
         *
         * ⚠ 服务端有两个坑（api-group.md §7.3）：
         *   · 群不存在时返回的是 `1013` 而不是 `1023`；
         *   · 成功后 `DEL chatgroup_<请求原串>`，`group_id` 带前导零会漏删。
         *   客户端一律传规范化后的纯数字串，避开第二个。
         * 成功时先给所有成员写系统消息「该群聊已解散」并推 1022，再 DEL。
         *
         * @return true = 已发出（结果看 \l groupDismissed）
         */
        bool dismissGroup(const QString& gid);

        /**
         * 获取群成员信息（1041）。
         *
         * 请求体 `{ "gid": "..." }` —— **字段名是 `gid`**，和本组其它接口的
         * `group_id` 不一致，服务端只读 `gid`，写成 `group_id` 会直接回 1020。
         *
         * 服务端先 `SISMEMBER` 校验"我在不在群里"，再查 MySQL。所以：
         *   · 我不在群里 → 1024；
         *   · **Redis 成员集合丢了 → 也回 1024**（哪怕我是真实成员）。
         *
         * @return true = 已发出（结果看 \l groupMembersReceived）
         */
        bool fetchGroupMembers(const QString& gid);

        /**
         * 获取群资料（1043）。
         *
         * 请求体 `{ "gid": "..." }` —— 与 1041 一样用 `gid`，不是 `group_id`。
         *
         * ⚠ 服务端**不校验成员身份**：只要登录了，知道群号就能查（api-group.md §13.13）；
         *   也**没有 Redis 缓存**，每次都会查 MySQL 的 `groups` 表。
         *   响应（1044）不带群号，由回包上下文补。
         *
         * @return true = 已发出（结果看 \l groupInfoReceived）
         */
        bool getGroupInfo(const QString& gid);

        /**
         * 修改群资料（1045）。只有群主 / 管理员能改。
         *
         * 请求体 `{ "gid": "..." }` + 可选 `name` / `avatar` / `announcement`。
         * **缺省（nullopt）= 不修改**；显式空串 = 清空（头像 / 公告）；
         * 群名传空串会直接 1020（服务端存储过程返回 -3）。
         *
         * ⚠ 与创建群聊的 `avator` 拼写不同，这里是 `avatar`。
         * ⚠ 数据库异常（-10 / -11）时回包**只有 msgid、没有 error**，
         *   这里统一按失败处理并回 \l ErrorCode::JsonError。
         * ⚠ 成功后**不写系统消息、不推 1022**，其他成员不会感知；要刷新得再发 1043。
         *
         * @return true = 已发出（结果看 \l groupInfoChangeResult）
         */
        bool changeGroupInfo(const QString& gid,
                             const std::optional<QString>& name,
                             const std::optional<QString>& avatar,
                             const std::optional<QString>& announcement);

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
         * 删除好友的结果（1027 → 1028）。
         *
         * 回包把请求里的 `uid` **原样回显**（成功、失败都带），所以按它认领；
         * 回显缺失（空串 / 老服务端）时退回请求侧记下的那一发。
         *
         * @param ok    error == 0：MySQL 与 Redis 好友缓存都已更新
         * @param error 1022 = 本来就不是好友；1013 = 数据库 / Redis 不可用；
         *              本地超时 = \l RequestTimeout
         */
        void friendDeleteResult(bool ok, int error, const QString& uid);

        /**
         * 设置好友备注的结果（1029 → 1030）。
         *
         * @param ok     error == 0
         * @param error  1022 = 对方不是好友 / 给自己设置备注；1020 = uid 非法；
         *               1013 = 数据库不可用；本地超时 = \l RequestTimeout
         * @param uid    请求里那个好友 uid（回包不回显它，只有 `id`）
         * @param remark 请求里那条新备注（成功时控制器用它就地更新好友列表）
         */
        void friendRemarkResult(bool ok, int error, const QString& uid, const QString& remark);

        /**
         * 编辑本人资料的结果（1031 → 1032）。
         *
         * @param ok    error == 0：Redis 与 MySQL 都已更新
         * @param error 1011 = 会话未登录 / MySQL 里没有这个 uid；1013 = 读不到用户缓存
         *              或数据库失败；本地超时 = \l RequestTimeout
         * @param name / desc / sex 请求里提交的值（回包只回显 id）
         */
        void userInfoChangeResult(bool ok, int error, const QString& name,
                                  const QString& desc, int sex);

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
         * ⚠ 推送体只有 `{"error":0}`：**没有发送者、没有类型、没有内容** ——
         *   收到之后必须发 1023 拉取（见 \l syncChatMessages），内容才拿得到。
         */
        void messageReceived();

        /**
         * 拉取回包（1024）。
         *
         * \note 服务端 PEL 优先重投（at-least-once）→ 同一条消息可能被拉到多次，
         *   去重是**调用方**的职责（按 `msgs[].id`）。
         *   迟到的回包仍照常解析上报：服务端已经把它投递过一次，这次不收就得等
         *   下一轮 PEL 重投（虽然现在不会丢，但白等一轮没意义）。
         */
        void chatMessagesReceived(const QList<mychat::protocol::ChatMessage>& messages, int error);

        /// 确认回包（1026）；error 非 0 时那批消息仍在服务端 PEL 里，下次拉取会重投
        void chatMessagesAcked(bool ok, int error);

        /**
         * 创建群聊的结果（1033 → 1034）。
         *
         * @param groupId 新群 id（**字符串**，服务端 `std::to_string`）；
         *                失败时为空串
         * @param name    请求里提交的群名（回包不带，靠请求侧补全）
         *
         * ⚠ Redis 写失败时服务端异常逃逸，回包只有 msgid、**没有 error** ——
         *   这种会走超时（\l RequestTimeout），调用方不要死等。
         */
        void groupCreated(bool ok, int error, const QString& groupId, const QString& name);

        /**
         * 拉人入群的结果（1035 → 1036）。
         *
         * 回包**不回传实际入群名单**（已经在群里的会被跳过），
         * 想确认结果得重新 \l fetchGroupMembers。
         *
         * @param uids 请求里提交的成员列表（原样带回，便于日志 / 本地状态）
         */
        void groupMembersAdded(bool ok, int error, const QString& gid, const QStringList& uids);

        /// 踢人的结果（1037 → 1038）；uid 是请求里那个被踢者
        void groupMemberKicked(bool ok, int error, const QString& gid, const QString& uid);

        /// 解散的结果（1039 → 1040）
        void groupDismissed(bool ok, int error, const QString& gid);

        /**
         * 群成员信息（1041 → 1042）。
         *
         * 空列表是**正常结果**（理论上不会，但别把空当成失败）；失败看 error：
         * 1024 = 不在群内 / Redis 成员集合丢失，1013 = 数据库不可用。
         */
        void groupMembersReceived(bool ok, int error, const QString& gid,
                                  const QList<mychat::protocol::GroupMember>& members);

        /**
         * 群资料（1043 → 1044）。
         *
         * 失败看 error：1023 群不存在、1013 数据库不可用、1020 gid 非法。
         * 成功时 \c info 的字段已解析好，`gid` 由请求上下文补上。
         */
        void groupInfoReceived(bool ok, int error, const mychat::protocol::GroupInfo& info);

        /**
         * 修改群资料的结果（1045 → 1046）。
         *
         * @param gid 请求里那个群（回包不带群号）
         */
        void groupInfoChangeResult(bool ok, int error, const QString& gid);

        /**
         * 一条请求在 kReplyTimeoutMs 内没有任何回包 —— 按服务端的约定，等价于
         * **这条请求被抛弃了**。
         *
         * 依据：2026-10-22 起所有处理器（含心跳）都由 `HANDLER_PREFIX` 统一回显
         * `msgid`，只要消息被取走处理就一定有回包；而消息队列（容量 10）满时
         * `try_send` 直接丢帧、既不回包也不断连。所以「超时 + 没有 msgid 回来」
         * 只可能是没被处理。
         *
         * @param msgid     该请求的编号（\l nextMsgId 生成，形如 `r-12`）
         * @param requestId 请求帧的 message_id，便于日志区分是哪类请求
         */
        void requestTimedOut(const QString& msgid, std::uint16_t requestId);

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
            /// 客户端生成的请求编号（服务端原样回显）；回包里缺失时为空
            QString msgid;
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
         * 登记「我刚发了 requestId（编号 msgid），等 replyId」。
         *
         * @param msgid     与请求一起发出的编号；回包带同一个值才认领得到
         * @param onTimeout 到期回调。**必须负责把调用方的等待状态收干净**
         *                 （m_auth_in_flight 之类的标记由回调自己清）。
         */
        void expectReply(std::uint16_t requestId, std::uint16_t replyId,
                         const QString& msgid, std::function<void()> onTimeout);
        /**
         * 某个回包到了：**优先按 msgid 精确认领**；回包没带编号（老服务端 /
         * 主动推送）时退回"同型号最早发出"的那条。
         *
         * \note 由 handleFrame 在最前面统一调用，各 handler 不用记得喊它 ——
         *       那就是"每加一条新消息都要记得加一行"的典型漏法。
         */
        void finishReply(std::uint16_t replyId, const QString& msgid);
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
        /// 删除好友回包（1028，回显 uid）
        void handleDelFriendResponse(const QByteArray& body);
        /// 设置好友备注回包（1030，只回 error + id）
        void handleSetFriendRemarkResponse(const QByteArray& body);
        /// 编辑本人资料回包（1032，只回 error + id）
        void handleChangeUserInfoResponse(const QByteArray& body);
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
        /// 拉取消息回包（1024）
        void handleSyncChatMessages(const QByteArray& body);
        /// 确认回包（1026，只有 error）
        void handleAckChatMessages(const QByteArray& body);
        /// 创建群聊回包（1034，error + group_id）
        void handleGroupCreateResponse(const QByteArray& body);
        /// 拉人回包（1036，只有 error）
        void handleGroupAddMembersResponse(const QByteArray& body);
        /// 踢人回包（1038，只有 error）
        void handleGroupKickMemberResponse(const QByteArray& body);
        /// 解散回包（1040，只有 error）
        void handleGroupDismissResponse(const QByteArray& body);
        /// 群成员信息回包（1042，error + mb_infos）
        /// 群成员回包（1042，error + mb_infos）
        void handleGroupMembersResponse(const QByteArray& body);

        /// 群资料回包（1044，error + name/avatar/owner_uid/announcement/maxMb/mbCnt/createAt）
        void handleGroupInfoResponse(const QByteArray& body);

        /// 修改群资料回包（1046，只有 error；数据库异常时连 error 都没有）
        void handleGroupInfoChangeResponse(const QByteArray& body);
        /**
         * 发送一条 JSON 请求，**自动注入客户端请求编号 msgid**。
         *
         * 服务端从 2026-10-22 起在所有响应里原样回显 `msgid`（含心跳与错误响应），
         * 所以每条请求都必须带上它 —— 它既是回包认领的依据，也是判断
         * "这条请求是不是被服务端丢了"的唯一凭据。
         *
         * @return 本次请求的 msgid；**空串表示没发出去**（未连接 / 超长）。
         *         调用方要把返回值原样交给 \l expectReply。
         */
        [[nodiscard]] QString sendJsonWithId(std::uint16_t message_id, const QJsonObject& body);
        /// 生成下一个请求编号（形如 `r-12`，连接内单调递增，重连不清零）
        [[nodiscard]] QString nextMsgId();
        /// 直接发一段已序列化好的 body（心跳要发体长 0 的空帧，走不了 sendJson）
        bool sendFrame(std::uint16_t message_id, const QByteArray& payload);
        /// 一次只发一个「查在线」请求；上一个回包了就接着发下一个
        void pumpOnlineQueue();
        /// 一次只发一条聊天消息；上一条回包（或超时）了就接着发下一条
        void pumpTextQueue();
        /// 一次只发一批确认；上一批回包了就接着发下一批
        void pumpAckQueue();

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

        /**
         * 同时在飞的群请求上限。
         *
         * 群请求按 msgid 配对、可以并发，但没有上限的话，一个不回包的服务端
         * 会让 \l m_group_requests 一直涨（每条要等 5s 才超时清掉）。
         * 界面一次最多点一两个按钮，8 条已经远超正常水位。
         */
        static constexpr int kMaxGroupRequestsInFlight = 8;

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

        /**
         * 请求编号的序号。
         *
         * 单调递增、\l reset 时**不清零**：断线重连后继续往下发，避免与上一条连接
         * 的迟到回包撞号（虽然旧连接已关，但服务端的处理可能正好跨界完成）。
         */
        quint64 m_msg_seq = 0;

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

        /*
         * 「删除好友」的回包（1028）会回显 uid，正常按回包认领。
         * 但回显字段缺失 / 空串时得有个兜底，否则界面上那次删除永远等不到结果。
         * 同一时刻只允许一个删除在飞（界面一次只能点一个按钮）。
         */
        QString m_del_friend_in_flight;

        /*
         * 「设置好友备注」的回包（1030）只带 error 与回显的 id —— 协议里的 id 是
         * 任意类型、服务端不做解析，拿它当请求序号要自己维护一套生成/比对逻辑。
         * 界面同一时刻只能改一个备注，串行就够：在飞时直接拒第二条，回包里
         * 用请求侧记下的这份数据补全信号。
         */
        QString m_remark_uid_in_flight;
        QString m_remark_value_in_flight;

        /*
         * 「编辑本人资料」（1032）同理：回包只有 error + id。
         * 一次只允许一个在飞 —— 编辑表单只有一个「保存」按钮。
         */
        bool m_profile_in_flight = false;
        QString m_profile_name_in_flight;
        QString m_profile_desc_in_flight;
        int m_profile_sex_in_flight = 2;

        /// 一条待发的聊天消息（1013）
        struct OutgoingText
        {
            /// 客户端生成的消息标识，服务端原样回显
            QString msg_id;
            QString to_uid;
            QString body;
            int type = 1;
            /// 群消息：1013 请求体里带 `"group": true`（JSON 布尔）
            bool group = false;
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

        /*
         * 拉取：一次只允许一个在飞。1024 里没有"请求序号"，并发拉取会分不清
         * 哪一批回包对应哪一发；而且拉取本身就是"有积压才拉"，串行完全够用。
         */
        bool m_pull_in_flight = false;

        /// 待确认的消息编号，按批（每批 ≤10）排队；一次只发一批
        QList<QStringList> m_ack_queue;
        bool m_ack_in_flight = false;

        /**
         * 群管理请求的请求侧上下文。
         *
         * 1034/1036/1038/1040/1042 的回包都**不回带群 id**（只有 1034 回 group_id），
         * 所以"这一发是给哪个群、拉的是谁"必须先存下来，回包时再拼回信号。
         *
         * 2026-10-22 起服务端在所有回包里原样回显 `msgid`，因此这份上下文按
         * **msgid** 存（`m_group_requests`），可以并发多条 —— 以前是单槽 + 串行，
         * 结果"建群刚回、正在拉人"时打开群成员面板会被直接拒掉。
         */
        struct GroupRequest
        {
            enum class Kind
            {
                Create,
                AddMembers,
                Kick,
                Dismiss,
                FetchMembers,
                GetInfo,
                ChangeInfo,
            };

            Kind kind = Kind::Create;
            QString gid;
            QString name;
            QString avatar;
            QString announcement;
            QStringList members;
            /// Kick 的目标
            QString target_uid;
        };

        /// msgid -> 群请求上下文；回包到达时 `take` 出来用，超时回调里 remove
        QHash<QString, GroupRequest> m_group_requests;

        /**
         * 取出这条群回包对应的请求上下文。
         *
         * 优先按回包里回显的 `msgid` 精确匹配；回包**没带编号**时（老服务端、
         * 或者服务端某条异常路径只回了半截）退回"唯一在飞的那条"——
         * 这跟 \l m_pending 的 FIFO 兜底是同一个思路：只有一条时不存在错配风险。
         *
         * @return 取不到（多条在飞且编号不认识）时返回默认构造的空上下文，
         *         调用方会拿着空 gid 上报 —— 一眼能看出没配上，不会张冠李戴。
         */
        [[nodiscard]] GroupRequest takeGroupRequest(const QByteArray& body);
    };
} // namespace mychat
