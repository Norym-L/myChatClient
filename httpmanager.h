#ifndef HTTPMANAGER_H
#define HTTPMANAGER_H

#include "singleton.h"
#include <QString>
#include <QUrl>
#include <QObject>
#include <QNetworkAccessManager>
#include <QJsonObject>
#include <QJsonDocument>

class HttpManager
    : public QObject
    , public TSingLeton<HttpManager>
{
    Q_OBJECT

public:
    ~HttpManager();

private:
    friend class TSingLeton<HttpManager>;
    HttpManager();
    QNetworkAccessManager m_manager;

    /**
     * @brief postHttpReq 发送网络亲求
     * @param url url
     * @param json 数据json
     * @param req_id 请求id
     * @param mod 模块
     */
    void postHttpReq(QUrl url, QJsonObject json, ReqId req_id, Modules mod);

private slots:
    void slot_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod);

signals:
    void sig_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod);
    void sig_reg_http_finish(ReqId id, QString res, ErrorCodes err);
};

#endif // HTTPMANAGER_H
