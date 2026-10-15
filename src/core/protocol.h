#pragma once

#include <QJsonValue>
#include <QString>

#include <cstdint>
#include <optional>

/**
 * MyChat 客户端的协议层：帧格式、消息编号、错误码与 JSON 辅助。
 *
 * 这里的取值刻意对齐**服务端实现**，而不是照抄 docs/ServerDocs/*.md ——
 * 两者存在已知分歧，详见 MessageId 的注释。
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
    /// 单帧 body 上限；超出服务端会关闭会话
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
    };

    /// 客户端内部请求编号（HTTP 用），与 MessageId 各自独立
    enum class ReqId : std::uint16_t
    {
        GetVerifyCode = 1001,
        RegisterUser,
        ResetPassword,
        LoginUser,
    };

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
    };

    /// 错误码 -> 用户可读提示
    [[nodiscard]] QString errorToTip(int code);

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
} // namespace mychat::protocol
