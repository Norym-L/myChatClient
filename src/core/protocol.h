#pragma once

#include <QJsonValue>
#include <QList>
#include <QString>

#include <cstdint>
#include <optional>

/**
 * MyChat 客户端的协议层：帧格式、消息编号、错误码与 JSON 辅助。
 *
 * 这里的取值刻意对齐**服务端实现**，而不是照抄 docs/ServerDocs 下的 md 文档 ——
 * 两者存在已知分歧，详见 MessageId 的注释。
 *
 * （上面那句别写成「目录 + 斜杠 + 通配星号」的形式：斜杠紧跟星号会被当成
 *   嵌套注释起始，GCC 会给 -Wcomment 警告。）
 */
namespace mychat::protocol
{
    // ══ TCP 帧格式 ════════════════════════════════════════════════════════
    // +----------------+----------------+---------------------------+
    // | message_id     | body_len       | body                      |
    // | 2 字节，大端序  | 2 字节，大端序  | body_len 字节，UTF-8 JSON |
    // +----------------+----------------+---------------------------+
    inline constexpr int HEAD_ID_LEN = 2;
    inline constexpr int HEAD_DATA_LEN = 2;
    inline constexpr int HEAD_TOTAL_LEN = HEAD_ID_LEN + HEAD_DATA_LEN;
    /**
     * **出站**（客户端 → 服务端）单帧 body 上限。
     *
     * 服务端接收侧的 `MAX_LENGTH` 就是 2048，超了它会直接 `clearSession` ——
     * 所以客户端**发**请求必须守这个数（见 ChatSocket::sendFrame）。
     *
     * ⚠ 这个值**不能**拿去卡入站：服务端 `CSession::SendNode` 写帧头时不校验长度，
     *   好友列表(1021) 这种随数据量增长的回包很容易超过它（约 15 个好友就到）。
     *   入站的长度上限由帧头字段宽度决定：body_len 是 2 字节，最大 65535。
     *   两件事不是同一个约束，别再合并成一个常量（合并过一次，症状是"好友一多就断线"）。
     */
    inline constexpr int MAX_BODY_LEN = 2048;

    /**
 * 线上 message_id。
 *
 * ⚠ 取值以**服务端实现**为准（GateServer/src/public/const.h 的 MessageType），
 *   而不是 docs/ServerDocs/api-tcp.md —— 后者写的是从 0 开始，与实现不符。
 *   发错的症状很隐蔽：服务端打印 `dealMessage() unknow message id` 之后
 *   **既不回包也不断开连接**，客户端表现为永久等待。
 */
    enum class MessageId : std::uint16_t
    {
        ChatLoginReq = 1001,
        ChatLoginRsp,
        SearchUserReq,
        SearchUserRsp,
        AddFriendReq,
        AddFriendRsp,
        SyncFriendRequestReq,
        SyncFriendRequestRsp,
        NotifyAddFriendReq,
        AuthFriendReq,
        AuthFriendRsp,
        NotifyAuthFriendReq,
        TextChatMsgReq,
        TextChatMsgRsp,
        NotifyTextChatMsgReq,
        HeartBeatReq,         // 1016
        HeartBeatRsp,         // 1017
        CheckIsUserOnlineReq, // 1018
        CheckIsUserOnlineRsp, // 1019
        SyncFriendListReq,    // 1020
        SyncFriendListRsp,    // 1021
        NotifyReceiveMessage, // 1022 —— 服务端推送「你有新消息」，**不含内容**
        SyncChatMessageReq,   // 1023 —— 拉取消息
        SyncChatMessageRsp,   // 1024
        AckChatMessageReq,    // 1025 —— 确认收到（服务端 XACK + XDEL）
        AckChatMessageRsp,    // 1026
        DelFriendReq,         // 1027 —— 删除好友（服务端 2026-10-19 新增）
        DelFriendRsp,         // 1028 —— 删除结果，回显 uid
        SetFriendRemarkReq,   // 1029 —— 设置好友备注（服务端 2026-10-21 新增）
        SetFriendRemarkRsp,   // 1030 —— 备注结果，原样回显 id
        ChangeUserInfoReq,    // 1031 —— 编辑本人资料（服务端 2026-10-21 新增）
        ChangeUserInfoRsp,    // 1032 —— 编辑结果，原样回显 id
        GroupCreateReq,       // 1033 —— 创建群聊（服务端 2026-10-23 新增）
        GroupCreateRsp,       // 1034 —— 回包带 group_id
        GroupAddMembersReq,   // 1035 —— 拉人入群
        GroupAddMembersRsp,   // 1036 —— 只回 error
        GroupKickMemberReq,   // 1037 —— 踢人
        GroupKickMemberRsp,   // 1038 —— 只回 error
        GroupDismissReq,      // 1039 —— 解散群聊
        GroupDismissRsp,      // 1040 —— 只回 error
        GetGroupUserInfoReq,  // 1041 —— 群成员信息（字段名是 gid）
        GetGroupUserInfoRsp,  // 1042 —— mb_infos
        GetGroupInfoReq,      // 1043 —— 群资料（字段名是 gid，2026-10-23 新增）
        GetGroupInfoRsp,      // 1044 —— name / avatar / owner_uid / announcement ...
        GroupInfoChangeReq,   // 1045 —— 修改群资料（name / avatar / announcement 可选）
        GroupInfoChangeRsp,   // 1046 —— 只回 error（数据库异常时连 error 都没有）
    };

    /**
     * 聊天消息类型（服务端 `chat_message::Type`），用于 1013 请求体的 `type` 字段。
     *
     * 服务端只接受 1~4，其它值直接回 `1020 REQUEST_VALUE_INVALID`。
     * 目前只有文本可用：图片 / 文件 / 表情包的编码方式（URL、文件 ID）服务端尚未约定，
     * 也没有上传通道 —— 枚举先占位，别在界面上放出发得出去的样子。
     */
    enum class ChatMessageType : std::uint16_t
    {
        Text = 1,
        Picture = 2,
        File = 3,
        Emoji = 4,
    };

    /**
     * 心跳发送间隔（毫秒）。
     *
     * 服务端用 Redis 的 `uip_<uid>` 键做在线标记，TTL = `HEART_BEAT_EXPIRE_SECONDS`（30 秒）：
     * 30 秒内没收到心跳，键过期，别人查你就是 offline，好友申请也不会被推送过来。
     *
     * 取 10 秒 = 每个 TTL 窗口有 3 次机会 —— 一次丢包/一次卡顿不至于把人打成离线。
     * 再密就是白耗流量，再疏（比如 25 秒）就没有容错余量了。
     */
    inline constexpr int HEARTBEAT_INTERVAL_MS = 10000;

    /**
     * 一次拉取（1023）或一次确认（1025）的**条数上限**。
     *
     * 服务端硬编码 10：`count > 10` 或 `ids` 超过 10 个直接回 `1020`。
     * 客户端的策略是"拉 10 条 → 确认这 10 条"，正好一批，不用分批。
     */
    inline constexpr int MAX_CHAT_MESSAGES_PER_BATCH = 10;

    /**
     * 好友备注的最大长度（字符）。
     *
     * 服务端文档没约定上限（由 `user_friend.remark` 的列宽决定）。客户端在发送前
     * 与界面输入框都按这个数卡住 —— 否则超长会被 MySQL 截断或直接报 1013，
     * 用户只会看到一句"数据库不可用"。
     */
    inline constexpr int MAX_FRIEND_REMARK_LEN = 64;

    /// 群名称的输入上限（字符）。服务端**不校验**长度（`api-group.md` §4.1），
    /// 但 MySQL 的列宽终归有限，超长会被截断或直接报 1013 —— 界面先卡住。
    inline constexpr int MAX_GROUP_NAME_LEN = 32;

    /// 群公告的输入上限（字符）。理由同上。
    inline constexpr int MAX_GROUP_ANNOUNCEMENT_LEN = 200;

    /**
     * 拉取回包（1024）里的一条消息。
     *
     * 字段与服务端 `syncChatMessageHandler` 的序列化一致；`id` 是 Redis Stream
     * 的编号（`<毫秒>-<序号>`），**确认（1025）时必须原样回传**。
     *
     * 群消息（2026-10-23 起）在此基础上多两个字段：
     *   · `group`    —— 布尔，true 表示群消息；
     *   · `group_id` —— 群 id，**只有群消息才有**（单聊不返回该字段）。
     * ⚠ 升级前已入队的群消息没有 `group_id`，这种历史消息按 `group=true` +
     *   空 groupId 处理：能确认，但没法归属到会话（见 ChatController）。
     */
    struct ChatMessage
    {
        QString id;
        std::uint64_t fromUid = 0;
        int type = 1;
        QString body;
        /// 群消息标记（`msgs[].group`）
        bool group = false;
        /// 群 id（`msgs[].group_id`）；单聊 / 历史群消息为空串
        QString groupId;

        [[nodiscard]] bool valid() const { return !id.isEmpty() && fromUid != 0; }
    };

    /// 拉取回包（1024）-> 消息列表；失败 / 空时返回空列表。
    /// （没有消息时服务端**不出现** `msgs` 字段，不是空数组。）
    [[nodiscard]] QList<ChatMessage> parseChatMessages(const QJsonObject& object);

    /// 客户端内部请求编号（HTTP 用），与 TCP 的 MessageId 各自独立
    enum class ReqId : std::uint16_t
    {
        GetVerifyCode = 1001,
        RegisterUser,
        ResetPassword,
        LoginUser,
    };

    /**
     * ReqId -> HTTP 路径。
     *
     * 路径集中在这一处，避免各控制器各写一份字符串字面量 —— 服务端改路径时只需改这里。
     * 注意 `/get_Verifycode` 的大小写就是服务端实际实现的写法（其余接口全小写），
     * 不要"顺手"统一成小写。
     */
    [[nodiscard]] QString reqPath(ReqId id);

    /**
 * 错误码。1 起是客户端本地码；1001 起与 ServerDocs/error-codes.md 一致，
 * HTTP / gRPC / TCP 三种响应共用同一套，0 表示成功。
 */
    enum class ErrorCode : std::uint16_t
    {
        Success = 0,

        // ── 客户端本地 ──
        JsonError = 1,
        NetworkError = 2,
        HttpStatusError = 3,
        RequestTooLong = 4,
        /// 发了请求但服务端在客户端的等待窗口内**一句都没回**。
        /// （服务端有几处分支是直接 return 不回包的，和家庭/服务器网络无关，
        ///  所以文案不能说成"网络异常"。）
        RequestTimeout = 5,

        // ── 服务端 ──
        ServerJsonError = 1001,
        RpcFailed = 1002,
        VerifyCodeExpired = 1003,
        VerifyCodeError = 1004,
        UserExist = 1005,
        PasswordError = 1006,
        EmailNotMatch = 1007,
        PasswordUpdateFailed = 1008,
        PasswordInvalid = 1009,
        RegisterUserFailed = 1010,
        UserUidInvalid = 1011,
        UserTokenInvalid = 1012,
        ServiceUnavailable = 1013,
        UserNotFound = 1014,
        CanNotAddYourselfAsFriend = 1015,
        UserIsYourFriendBefore = 1016,
        CheckYourReceivedFriendRequest = 1018,
        UserOutline = 1019,
        /// 请求数据非法（处理好友申请时 state 不是 1/2 等）
        RequestValueInvalid = 1020,
        /// 重复处理：该条好友申请已经被处理过
        DoubleHandleRequest = 1021,
        /**
         * 不是好友关系（服务端 2026-10-19 新增 `NOT_FRIEND`）。
         *
         * 四个出处：
         *   · 发消息（1013）的好友校验没过 —— 消息**不会**写进对方离线流；
         *   · 删好友（1027）时双方本来就不是好友；
         *   · 设置好友备注（1029）时对方不是好友，或者试图给自己设置备注。
         *   · **群消息（1013 且 `group = true`）发送时不在群内**（2026-10-23 起复用，
         *     见 api-group.md §9.1）。
         *
         * ⚠ 这个数值和「新消息推送」的 message_id 1022 撞号，但那是两套独立枚举
         *   （错误码 vs 消息编号），别混用。
         */
        NotFriend = 1022,
        /// 群 id 非法或群不存在（1036 / 1038 / 1040）
        GroupIdInvalid = 1023,
        /// 权限不足：不是群主 / 管理员，或者**不在群内**（1036 / 1038 / 1040 / 1042）
        NoPower = 1024,
        /// 群已满（服务端**定义了但没用**：拉人时群满实际返回 1013，见 api-group.md §11.6）
        GroupFulled = 1025,
    };

/// 错误码 -> 用户可读提示
[[nodiscard]] QString errorToTip(int code);

/**
 * `/get_Verifycode` 专用的错误码 -> 提示。
 *
 * 这个接口的 `error` 语义与其它接口**不一样**：GateServer 会把 VerifyServer
 * gRPC 返回的 `rsp.error()` 原样透传（见 LogicSystem 里 `root["error"] = rsp.error()`），
 * 所以非零值往往是个小数字（如 1），并不是我们 ErrorCode 里的 1001/1002。
 * 直接套 errorToTip 会把它误报成「服务器返回数据解析失败」—— 排查时极容易带偏方向。
 */
[[nodiscard]] QString verifyCodeErrorToTip(int code);

/**
 * 从 JSON 字段解析 uid。
 *
 * 约定：uid 一律以 **uint64 的字符串**形式传递 —— QJsonValue 内部用 double 存数字，
 * 只有 53 位有效整数，超过 2^53 会静默丢精度。为兼容旧服务端也接受数字形式，
 * 但超出 double 精确范围时会记 WARN。
 *
 * @return 字段缺失、类型不符或字符串非法时返回 std::nullopt
 */
    [[nodiscard]] std::optional<std::uint64_t> parseUid(const QJsonValue& value);

    /// POST /user_login 响应里分配到的 ChatServer 信息
    struct ChatServerInfo
    {
        std::uint64_t uid = 0;
        QString token;
        QString host;
        QString port;
    };

    /// 搜索用户回包里的用户信息（MESSAGE_SEARCH_USER_RSP）
    struct UserInfo
    {
        std::uint64_t uid = 0;
        QString name;
        /// 个性签名
        QString desc;
        /// 0 男 / 1 女 / 其余按未知处理
        int sex = 2;
        /// 头像地址；服务端目前没有头像服务，通常为空串
        QString icon;

        [[nodiscard]] bool valid() const { return uid != 0; }
    };

    /// 好友申请列表里的一项（MESSAGE_SYNC_FRIEND_REQUEST_RSP 的 apply_infos[]）
    struct FriendRequest
    {
        /// 申请记录 id
        std::uint64_t id = 0;
        std::uint64_t fromUid = 0;
        std::uint64_t toUid = 0;
        QString name;
        /**
         * 备注（`nick` 字段，2026-10-17 服务端新增）。

         * **只有发送方视角有值**：申请人自己给接收人起的备注。
         * 接收人看到的那条永远是空串 —— 别拿它当"对方给我起的名字"。
         */
        QString nick;
        /// 对方填写的验证信息
        QString verifyInfo;
        QString icon;
        int sex = 2;
        /// 申请状态（MySQL 里的 status）
        int state = 0;

        [[nodiscard]] bool valid() const { return id != 0; }
    };

    /**
     * 好友列表里的一项（MESSAGE_SYNC_FRIEND_LIST_RSP 的 friends[]，1020/1021）。
     *
     * 字段与服务端 `LogicSystem::syncUserFriendList` 的实际序列化一致
     * （`uid` / `name` / `desc` / `nick` / `sex` / `icon`）。
     */
    struct FriendInfo
    {
        std::uint64_t uid = 0;
        QString name;
        /// 好友签名
        QString desc;
        /// **我给这个好友设置的备注**（服务端 `user_friend.remark`），没设置时为空串
        QString nick;
        /// 0 男 / 1 女 / 其余（服务端 DAO 把 NULL 写成 3）按未知处理
        int sex = 2;
        QString icon;

        [[nodiscard]] bool valid() const { return uid != 0; }
    };

    /// 搜索用户回包 -> UserInfo 列表；未找到或失败时返回空列表。
    /// 服务端命中时返回 {"error":0, "infos":[{uid,user_name,desc,sex,icon}, ...]}
    /// （可能多条），没找到时只回 {"error":1014}。
    [[nodiscard]] QList<UserInfo> parseSearchUserRsp(const QJsonObject& object);

    /// 好友申请列表回包 -> FriendRequest 数组
    [[nodiscard]] QList<FriendRequest> parseFriendRequestList(const QJsonObject& object);

    /**
     * 好友列表回包 -> FriendInfo 数组（1021）。
     *
     * ⚠ 服务端有三个"看着像 bug、其实是现状"的写法，这里全部兜住：
     *   · `count` 是**字符串**（`std::to_string`），不能当数字读；
     *   · 没有好友时 `friends` 是 **null**（不是"字段缺席"，也不是空数组）；
     *   · 好友资料查不到的那条服务端会跳过，所以 count 可能大于实际条数。
     */
    [[nodiscard]] QList<FriendInfo> parseFriendList(const QJsonObject& object);

    /**
     * 群成员（1042 的 `mb_infos[]`）。
     *
     * 服务端只给四个字段（见 `api-group.md` §8.2）：
     *   · `uid`     —— 成员 uid（字符串）
     *   · `nick_g`  —— **群内昵称**（`group_members.nickname`），不是账号昵称；
     *                  数据库里为 NULL 时服务端给空串
     *   · `join_at` —— 入群时间，**毫秒时间戳字符串**（不是 RFC 时间）
     *   · `role`    —— 0 成员 / 1 管理员 / 2 群主
     *
     * ⚠ 没有头像、没有账号昵称 —— 要显示这些得另查好友列表或搜索接口。
     */
    struct GroupMember
    {
        std::uint64_t uid = 0;
        QString nickG;
        qint64 joinAtMs = 0;
        int role = 0;

        [[nodiscard]] bool valid() const { return uid != 0; }
        [[nodiscard]] bool isOwner() const { return role == 2; }
    };

    /**
     * 群成员信息回包（1042）-> GroupMember 数组。
     *
     * ⚠ 没有成员时服务端**不出现** `mb_infos` 字段（和 1021 的 friends 不同，
     *   friends 是 null）；两种都走空数组。
     * ⚠ role 是**数字**（服务端直接 `value["role"] = role`），不是字符串。
     */
    [[nodiscard]] QList<GroupMember> parseGroupMembers(const QJsonObject& object);

    /**
     * 群资料（1044 的响应）。
     *
     * 服务端字段（见 `api-group.md` §9.2）：
     *   · `name` / `avatar` / `owner_uid` / `announcement` —— 字符串；
     *   · `maxMb` / `mbCnt` —— **字符串**（服务端 `to_string`），分别是最大人数与当前人数；
     *   · `createAt` —— 建群时间，**毫秒时间戳字符串**（不是 RFC 时间）；
     *   · 响应**不带 gid** —— 由调用方按请求上下文补。
     *
     * ⚠ `mbCnt` 来自 `groups.member_count` 冗余列，不是对 `group_members` 的实时统计；
     *   和 1042 返回的成员列表长度不一致时，以成员列表为准。
     * ⚠ `avatar` / `announcement` 数据库为 NULL 时服务端回空串，不是 null。
     */
    struct GroupInfo
    {
        QString gid;
        QString name;
        QString avatar;
        QString ownerUid;
        QString announcement;
        int maxMembers = 0;
        int memberCount = 0;
        qint64 createdAtMs = 0;
        bool valid = false;
    };

    /**
     * 群资料回包（1044）-> GroupInfo。
     *
     * 注意这里只解析字段，**不校验 error**：错误码由 ChatSocket 先判，
     * 失败时不会调用本函数。`gid` 也不在回包里，调用方拿到后自己补。
     */
    [[nodiscard]] GroupInfo parseGroupInfo(const QJsonObject& object);

    /**
     * 「查询用户是否在线」回包。
     *
     * 服务端回的字段是 `error`（SUCCESS / SERVICE_UNAVAILABLE）与
     * `online`（**字符串** "online" / "offline"，不是布尔）。
     *
     * @return 有 error 且非 0、或 online 字段缺失/取值不认识时返回 std::nullopt
     */
    [[nodiscard]] std::optional<bool> parseUserOnlineRsp(const QJsonObject& object);
} // namespace mychat::protocol
