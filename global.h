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
extern std::function<void(QWidget *)> repolish;

/**
 * @brief The ReqId enum 请求
 */
enum ReqId
{
    ID_GET_VERIFY_CODE = 1001, // 获取验证码
    ID_REG_USER = 1002, // 注册用户
    ID_RESET_PWD = 1003, // 重置密码
    ID_LOGIN_USER = 1004, // 用户登录
};

/**
 * @brief The Modules enum 模块
 */
enum Modules
{
    REGISTERMOD = 0, // 注册模块
    RESETMOD = 1, // 重置密码模块
    LOGINMOD = 2, // 登录模块
};

/**
 * @brief The ErrorCodes enum 异常码
 */
enum ErrorCodes
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
};

/// 注册页面错误提示
enum class TipErr
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
enum class ClickLabelState
{
    NORMAL = 0,
    SELECT = 1,
};


extern QString GATE_URL_PERFIX;

#endif // GLOBAL_H
