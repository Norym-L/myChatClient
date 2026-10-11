#ifndef GLOBAL_H
#define GLOBAL_H

#include <QWidget>
#include <cstdint>
#include <functional>
#include <QRegularExpression>
#include "QStyle"
#include <memory>
#include <iostream>
#include <mutex>
#include <optional>
#include <QByteArray>
#include <QNetworkReply>
#include <QJsonObject>
#include <QDir>
#include <QSettings>

/**
 * @brief repolish 刷新qss
 */
extern std::function<void(QWidget*)> repolish;

/**
 * @brief 应用主题（深色 / 浅色）
 * @note 样式表结构：assets/styles/common.qss（结构层：尺寸/圆角/字体）
 *       + assets/styles/themes/{dark,light}.qss（主题层：全部颜色与图标），
 *       由 applyTheme() 拼接后 setStyleSheet 到 qApp。
 */
enum class AppTheme : std::uint8_t
{
    Dark,
    Light,
};

/**
 * @brief applyTheme 应用主题：拼接 common.qss + 主题 qss 并设置到 qApp，
 *        同时刷新调色板的占位文字色（PlaceholderText QSS 管不到）
 */
void applyTheme(AppTheme theme);

/**
 * @brief currentTheme 当前主题（未调用 applyTheme 前为 Dark）
 */
[[nodiscard]] AppTheme currentTheme();

/**
 * @brief themedIconPath 代码内图标按主题取路径（QSS 管不到的 QAction 图标等）
 * @note Dark -> ":/res/<name>.png"；Light -> ":/assets/images/themes/light/<name>.png"
 */
[[nodiscard]] QString themedIconPath(const QString& name);

/**
 * @brief The ReqId enum 请求
 * @note 客户端内部请求编号，与 ChatServer 线上 message_id 不是同一套编号，
 *       两者的换算见 ChatProtocol::toMessageId / ChatProtocol::toReqId。
 */
enum ReqId : std::uint16_t
{
    ID_GET_VERIFY_CODE = 1001, // 获取验证码
    ID_REG_USER, // 注册用户
    ID_RESET_PWD, // 重置密码
    ID_LOGIN_USER, // 用户登录
    ID_CHAT_LOGIN_REQ, // 聊天系统登录
    ID_CHAT_LOGIN_RSP, // 聊天系统登录回包
    ID_SEARCH_USER_REQ, // 查询用户请求
    ID_SEARCH_USER_RSP, // 查询用户请求回包
    ID_ADD_FRIEDN_REQ, // 添加好友请求
    ID_ADD_FRIEDN_RSP, // 添加好友请求回包
    ID_NOTIFY_ADD_FRIEND_REQ, // 通知用户添加好友申请
    ID_AUTH_FRIEND_REQ, // 认证好友请求
    ID_AUTH_FRIEND_RSP, // 认证好友请求回包
    ID_NOTIFY_AUTH_FRIEND_REQ, // 通知用户认证好友申请
    ID_TEXT_CHAT_MSG_REQ, // 文本聊天信息请求
};

/**
 * @brief The Modules enum 模块
 */
enum Modules : std::uint8_t
{
    REGISTER_MOD = 0, // 注册模块
    RESET_MOD = 1, // 重置密码模块
    LOGIN_MOD = 2, // 登录模块
};

/**
 * @brief The ErrorCodes enum 异常码
 * @note 1 及以上的小数值为客户端本地错误码；1001 起与 ServerDocs/error-codes.md
 *       完全一致，服务端 HTTP / gRPC / TCP 三种响应共用同一套编码，成功为 0。
 */
enum ErrorCodes : std::uint16_t
{
    SUCCESS = 0, // 成功
    ERR_JSON = 1, // json 解析失败
    ERR_NETWORK = 2, // 网络错误
    ERR_HTTP_STATUS = 3, // 服务端返回非 200（未知路径 404 / 不支持的方法 405）
    ERR_REQUEST_TOO_LONG = 4, // 请求体超出服务端缓冲区上限

    /// 服务器返回值
    ERROR_JSON = 1001, // Json解析错误或必填字段缺失
    RPC_FAILED = 1002, // RPC请求失败
    VERIFY_CODE_EXPIRED = 1003, // 验证码过期
    VERIFY_CODE_ERROR = 1004, // 验证码错误
    USER_EXIST = 1005, // 用户已存在
    PASSWORD_ERROR = 1006, // 密码错误
    EMAIL_NOT_MATCH = 1007, // 邮箱不匹配
    PASSWORD_UP_FAILED = 1008, // 密码修改失败
    PASSWORD_INVALID = 1009, // 密码无效
    REGISTER_USER_FAILED = 1010, // 用户注册失败
    USER_UID_INVALID = 1011, // 用户id非法或不存在
    USER_TOKEN_INVALID = 1012, // token非法或已失效
    SERVICE_UNAVAILABLE = 1013, // 依赖服务不可用（数据库/Redis连接池超时或不可达）
    USER_NOT_FOUND = 1014, // 未找到查询的用户
};

/**
 * @brief ChatServer 私有 TCP 帧协议（详见 ServerDocs/api-tcp.md）
 *
 * 帧格式（双向一致）：
 * +----------------+----------------+---------------------------+
 * | message_id     | body_len       | body                      |
 * | 2 字节，大端序 | 2 字节，大端序 | body_len 字节，JSON 文本  |
 * +----------------+----------------+---------------------------+
 */
namespace ChatProtocol
{
inline constexpr int HEAD_ID_LEN = 2; // message_id 长度
inline constexpr int HEAD_DATA_LEN = 2; // body_len 长度
inline constexpr int HEAD_TOTAL_LEN = HEAD_ID_LEN + HEAD_DATA_LEN; // 头部总长
inline constexpr int MAX_LENGTH = 2048; // body 最大长度，超出服务端会关闭会话

/// 线上 message_id
enum MessageId : std::uint16_t
{
    MESSAGE_CHAT_LOGIN_REQ = 1001,    // 登录请求
    MESSAGE_CHAT_LOGIN_RSP,           // 登录回包
    MESSAGE_SEARCH_USER_REQ,          //用户搜索请求
    MESSAGE_SEARCH_USER_RSP,          //搜索用户回包
    MESSAGE_ADD_FRIEND_REQ,           //申请添加好友请求
    MESSAGE_ADD_FRIEND_RSP,           //申请添加好友回复
    MESSAGE_NOTIFY_ADD_FRIEND_REQ,    //通知用户添加好友申请
    MESSAGE_AUTH_FRIEND_REQ,          //认证好友请求
    MESSAGE_AUTH_FRIEND_RSP,          //认证好友回复
    MESSAGE_NOTIFY_AUTH_FRIEND_REQ,   //通知用户认证好友申请
    MESSAGE_TEXT_CHAT_MSG_REQ,        //文本聊天信息请求
    MESSAGE_TEXT_CHAT_MSG_RSP,        //文本聊天信息回复
    MESSAGE_NOTIFY_TEXT_CHAT_MSG_REQ, //通知用户文本聊天信息
};

/**
 * @brief toMessageId 客户端请求编号 -> 线上 message_id
 * @return 无对应线上编号时返回 std::nullopt
 */
[[nodiscard]] std::optional<std::uint16_t> toMessageId(ReqId req_id);

/**
 * @brief toReqId 线上 message_id -> 客户端请求编号
 * @return 未知编号时返回 std::nullopt
 */
[[nodiscard]] std::optional<ReqId> toReqId(std::uint16_t message_id);
} // namespace ChatProtocol

/**
 * @brief errorCodeToTip 服务器 error 字段 -> 用户可读提示
 */
[[nodiscard]] QString errorCodeToTip(int error_code);

/**
 * @brief parseUid 从 JSON 字段解析用户 uid（uint64）
 *
 * 约定（见根目录《接口文档.md》）：uid 一律以 **uint64 的字符串形式**传递。
 * 原因：QJsonValue 内部用 double 存储数字，只有 53 位有效整数，超过 2^53 的
 * uid 会静默丢精度，因此不能依赖数字形式。
 *
 * 为兼容尚未切换的服务端，这里同时接受数字形式，但仅当值落在 double 可精确
 * 表示的整数区间内才可信，超出会记 WARN（此时应视为服务端未按约定发送）。
 *
 * @return 字段缺失、类型不符或字符串非法时返回 std::nullopt
 */
[[nodiscard]] std::optional<std::uint64_t> parseUid(const QJsonValue& value);

/// 登录页面错误提示
enum class TipErr : std::uint8_t
{
    TIP_SUCCESS = 0,
    TIP_EMAIL_ERR = 1,
    TIP_PWD_ERR = 2,
    TIP_CONFIRM_ERR = 3,
    TIP_PWD_CONFIRM = 4,
    TIP_VERIFY_ERR = 5,
    TIP_USER_ERR = 6
};

/// label选择状态
enum class ClickLabelState : std::uint8_t
{
    NORMAL = 0,
    SELECT = 1,
};

/// /user_login 响应中分配到的 ChatServer 信息
struct ServerInfo
{
    std::uint64_t _uid = 0;
    QString _token;
    QString _host;
    QString _port;
};

struct MessageInfo
{
    enum class MessageFlag
    {
        TEXT,
        IMAGE,
        FILE
    }; // text, image, file

    MessageFlag messageFlag;
    QString content; // 文件、图片的url， 文本信息
    QPixmap pixmap; // 文件、图片缩略图
};

#define PICTURE_MAX_WIDTH   200
#define PICTURE_MAX_HEIGHT  100


extern QString GATE_URL_PREFIX;

#endif // GLOBAL_H
