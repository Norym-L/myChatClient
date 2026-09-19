#ifndef GLOBAL_H
#define GLOBAL_H

#include <QWidget>
#include <functional>
#include <QRegularExpression>
#include "QStyle"
#include <memory>
#include <iostream>
#include <mutex>
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
 * @brief The ReqId enum 请求
 */
enum ReqId : std::uint16_t
{
	ID_GET_VERIFY_CODE = 1001, // 获取验证码
	ID_REG_USER = 1002, // 注册用户
	ID_RESET_PWD = 1003, // 重置密码
	ID_LOGIN_USER = 1004, // 用户登录
	ID_CHAT_LOGIN = 1005, // 聊天系统登录
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
 */
enum ErrorCodes : std::uint16_t
{
	SUCCESS = 0, // 成功
	ERR_JSON = 1, // json 解析失败
	ERR_NETWORK = 2, // 网络错误

	/// 服务器返回值
	ERROR_JSON = 1001, // Json解析错误
	RPC_FAILED = 1002, // RPC请求失败
	VERIFY_CODE_EXPIRED = 1003, // 验证码过期
	VERIFY_CODE_ERROR = 1004, // 验证码错误
	USER_EXIST = 1005, // 用户已存在
	PASSWORD_ERROR = 1006, // 密码错误
	EMAIL_NOT_MATCH = 1007, // 邮箱不匹配
	PASSWORD_UP_FAILED = 1008, // 密码修改失败
	PASSWORD_INVALID = 1009, // 密码无效
	REGISTER_USER_FAILED = 10010, // 用户注册失败
};

/// 注册页面错误提示
enum class TipErr : std::uint8_t
{
	TIP_SUCCESS = 0,
	TIP_EMAIL_ERR = 1,
	TIP_PWD_ERR = 2,
	TIP_CONFIRM_ERR = 3,
	TIP_PWD_CONFIRM = 4,
	TIP_VARIFY_ERR = 5,
	TIP_USER_ERR = 6
};

/// label选择状态
enum class ClickLabelState : std::uint8_t
{
	NORMAL = 0,
	SELECT = 1,
};

struct ServerInfo
{
	int _uid;
	QString _token;
	QString _host;
	QString _port;
};


extern QString GATE_URL_PERFIX;

#endif // GLOBAL_H
