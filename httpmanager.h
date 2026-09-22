#ifndef HTTPMANAGER_H
#define HTTPMANAGER_H

#include "singleton.h"
#include "global.h"

/**
 * @brief HttpManager GateServer HTTP 接口（详见 ServerDocs/api-http.md）
 *
 * - Base URL：http://<gate-host>:8080
 * - 请求体为 application/json
 * - 成功的判定是 HTTP 200 + 响应体 error == 0，业务失败同样是 HTTP 200
 */
class HttpManager
    : public QObject
    , public Singleton<HttpManager>
{
    Q_OBJECT

public:
    ~HttpManager() override;

    /**
     * @brief postHttpReq 发送网络请求
     * @param url 完整请求地址
     * @param json 请求体
     * @param req_id 请求id
     * @param mod 模块
     */
    void postHttpReq(const QUrl& url, const QJsonObject& json, ReqId req_id, Modules mod);

private:
    friend class Singleton<HttpManager>;
    HttpManager();

    /// 客户端超时（文档：服务端请求读取超时 60 秒）
    static constexpr int HTTP_TIMEOUT_MS = 10000;
    /// 文档：服务端请求体读取缓冲区 8192 字节，超出会被中间件拒绝
    static constexpr int MAX_REQ_BODY_LEN = 8192;

    QNetworkAccessManager m_manager;

private slots:
    void slot_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod);

signals:
    void sig_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod);
	void sig_reg_http_finish(ReqId id, QString res, ErrorCodes err);
    void sig_reset_mod_finish(ReqId id, QString res, ErrorCodes err);
    void sig_login_mod_finish(ReqId id, QString res, ErrorCodes err);
};

#endif // HTTPMANAGER_H
