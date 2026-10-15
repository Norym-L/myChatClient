#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>

#include <functional>

class QNetworkAccessManager;

namespace mychat
{
    /**
 * GateServer 的 HTTP 客户端。
 *
 * 约定（见 docs/ServerDocs/api-http.md）：
 *   - Base URL 形如 http://127.0.0.1:8080，请求体为 application/json
 *   - **业务失败同样是 HTTP 200**，要再看响应体里的 error 字段（0 为成功）
 *   - 未知路径返回 404 + text/plain
 *
 * 回调式接口：post() 一次性接收「成功/失败 + 结果对象」，调用方不必自己管
 * QNetworkReply 的生命周期。
 */
    class HttpClient : public QObject
    {
        Q_OBJECT

    public:
        /// 完成回调。ok 为 true 时 resp 是服务端返回的 JSON 对象；
    /// ok 为 false 时 resp 只含一个 "error" 字段（本地错误码）。
        using Callback = std::function<void(bool ok, const QJsonObject & resp)>;

        static HttpClient& instance();

        /// 设置 GateServer 前缀，例如 "http://127.0.0.1:8080"
        void setBaseUrl(const QString& base_url);
        [[nodiscard]] QString baseUrl() const { return m_base_url; }

        /**
     * POST 一个 JSON 请求。
     * @param path 以 / 开头的路径，如 "/user_login"
     * @param body 请求体
     * @param cb   完成回调（在 Qt 事件循环所在线程执行）
     */
        void post(const QString& path, const QJsonObject& body, Callback cb);

    private:
        explicit HttpClient(QObject* parent = nullptr);

        /// 文档：服务端请求读取超时 60 秒；客户端取更短，避免界面长时间无反馈
        static constexpr int kTimeoutMs = 10000;
        /// 文档：服务端请求体读取缓冲区 8192 字节，超出会被中间件拒绝
        static constexpr int kMaxBodyLen = 8192;

        QNetworkAccessManager* m_manager = nullptr;
        QString m_base_url;
    };
} // namespace mychat
