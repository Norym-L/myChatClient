#include "HttpClient.h"

#include <QDebug>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include "protocol.h"

namespace mychat
{
    HttpClient& HttpClient::instance()
    {
        static HttpClient client;
        return client;
    }

    HttpClient::HttpClient(QObject* parent)
        : QObject(parent)
          , m_manager(new QNetworkAccessManager(this))
    {
    }

    void HttpClient::setBaseUrl(const QString& base_url)
    {
        m_base_url = base_url;
        qInfo() << "[http] gate server:" << m_base_url;
    }

    void HttpClient::post(const QString& path, const QJsonObject& body, Callback cb)
    {
        const auto fail = [cb](const protocol::ErrorCode code)
        {
            QJsonObject result;
            result.insert(QStringLiteral("error"), static_cast<int>(code));
            if (cb)
                cb(false, result);
        };

        if (m_base_url.isEmpty())
        {
            qWarning() << "[http] base url 未设置，请求被丢弃:" << path;
            fail(protocol::ErrorCode::NetworkError);
            return;
        }

        const QByteArray payload = QJsonDocument(body).toJson(QJsonDocument::Compact);
        if (payload.size() > kMaxBodyLen)
        {
            qWarning() << "[http] 请求体过大:" << payload.size() << ">" << kMaxBodyLen;
            fail(protocol::ErrorCode::RequestTooLong);
            return;
        }

        QNetworkRequest request{QUrl(m_base_url + path)};
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        request.setTransferTimeout(kTimeoutMs);

        qDebug() << "[http] POST" << path << QString::fromUtf8(payload);

        QNetworkReply* reply = m_manager->post(request, payload);

        connect(reply, &QNetworkReply::finished, this, [reply, path, fail, cb]()
        {
            reply->deleteLater();

            const int http_status =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = reply->readAll();

            if (reply->error() != QNetworkReply::NoError && http_status == 0)
            {
                qWarning() << "[http] 网络错误:" << path << reply->errorString();
                fail(protocol::ErrorCode::NetworkError);
                return;
            }

            if (http_status != 200)
            {
                qWarning() << "[http] HTTP 状态异常:" << path << http_status
                    << QString::fromUtf8(data.left(120));
                fail(protocol::ErrorCode::HttpStatusError);
                return;
            }

            qDebug() << "[http] 响应" << http_status << QString::fromUtf8(data);

            QJsonParseError parse_error{};
            const QJsonDocument document = QJsonDocument::fromJson(data, &parse_error);
            if (parse_error.error != QJsonParseError::NoError || !document.isObject())
            {
                qWarning() << "[http] 响应解析失败:" << parse_error.errorString();
                fail(protocol::ErrorCode::JsonError);
                return;
            }

            if (cb)
                cb(true, document.object());
        });
    }
} // namespace mychat
