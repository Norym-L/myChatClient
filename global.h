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

extern QString GATE_URL_PERFIX;

#endif // GLOBAL_H
