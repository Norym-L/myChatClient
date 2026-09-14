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
};

/**
 * @brief The Modules enum 模块
 */
enum Modules
{
    REGISTERMOD = 0, // 注册模块
};

/**
 * @brief The ErrorCodes enum 异常码
 */
enum ErrorCodes
{
    SUCCESS = 0, // 成功
    ERR_JSON = 1, // json 解析失败
    ERR_NETWORK = 2, // 网络错误
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
