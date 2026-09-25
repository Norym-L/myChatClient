#include "network/httpmanager.h"

#include <QJsonDocument>

#include "core/logger.h"

HttpManager::~HttpManager()
{

}

HttpManager::HttpManager()
{
	connect(this, &HttpManager::sig_http_finish, this, &HttpManager::slot_http_finish);
}

void HttpManager::postHttpReq(const QUrl& url, const QJsonObject& json, const ReqId req_id, const Modules mod)
{
	const QByteArray data = QJsonDocument(json).toJson(QJsonDocument::Compact);
	if (data.size() > MAX_REQ_BODY_LEN)
	{
		LOG_WARN() << "[http] request body is too long:" << data.size() << "url:" << url.toString();
		emit this->sig_http_finish(req_id, "", ErrorCodes::ERR_REQUEST_TOO_LONG, mod);
		return;
	}

	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setTransferTimeout(HTTP_TIMEOUT_MS);

	LOG_DEBUG() << "[http] post" << url.toString() << data;

	QNetworkReply* reply = m_manager.post(request, data);
	QObject::connect(reply, &QNetworkReply::finished, this, [this, reply, req_id, mod]()
		{
			const int status_code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
			const QNetworkReply::NetworkError net_error = reply->error();
			const QString error_string = reply->errorString();
			const QString url_string = reply->url().toString();
			const QByteArray body = reply->readAll();
			reply->deleteLater();

			// 文档：未知路径 404（url not found）、不支持的方法 405（method not allowed），
			// 这两种情况响应体不是 JSON，不能按业务响应解析
			if (status_code == 0)
			{
				LOG_WARN() << "[http] network error:" << error_string << "url:" << url_string;
				emit this->sig_http_finish(req_id, "", ErrorCodes::ERR_NETWORK, mod);
				return;
			}

			if (status_code != 200)
			{
				LOG_WARN() << "[http] unexpected http status:" << status_code
					<< "network error:" << net_error << "body:" << body;
				emit this->sig_http_finish(req_id, "", ErrorCodes::ERR_HTTP_STATUS, mod);
				return;
			}

			// 文档：业务失败也是 HTTP 200，由调用方根据响应体的 error 字段区分
			LOG_DEBUG() << "[http] response" << status_code << QString::fromUtf8(body);
			emit this->sig_http_finish(req_id, QString::fromUtf8(body), ErrorCodes::SUCCESS, mod);
		});
}

void HttpManager::slot_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod)
{
	if (mod == Modules::REGISTER_MOD)
	{
		// 发送信号通知注册模块http响应结束
		emit sig_reg_http_finish(id, res, err);
	}
	else if (mod == Modules::RESET_MOD)
	{
		emit sig_reset_mod_finish(id, res, err);
	}
	else if (mod == Modules::LOGIN_MOD)
	{
		emit sig_login_mod_finish(id, res, err);
	}
}
