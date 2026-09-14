#ifndef HTTPMANAGER_H
#define HTTPMANAGER_H

#include "singleton.h"
#include "global.h"

class HttpManager
    : public QObject
    , public Singleton<HttpManager>
{
    Q_OBJECT

public:
    ~HttpManager();

    /**
     * @brief postHttpReq 发送网络亲求
     * @param url url
     * @param json 数据json
     * @param req_id 请求id
     * @param mod 模块
     */
    void postHttpReq(QUrl url, QJsonObject json, ReqId req_id, Modules mod);

private:
    friend class Singleton<HttpManager>;
    HttpManager();
    QNetworkAccessManager m_manager;

private slots:
    void slot_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod);

signals:
    void sig_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod);
    void sig_reg_http_finish(ReqId id, QString res, ErrorCodes err);
    void sig_reset_mod_finish(ReqId id, QString res, ErrorCodes err);
};

#endif // HTTPMANAGER_H
