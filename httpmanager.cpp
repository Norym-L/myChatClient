#include "httpmanager.h"

#include "global.h"

HttpManager::~HttpManager()
{

}

HttpManager::HttpManager()
{
	connect(this, &HttpManager::sig_http_finish, this, &HttpManager::slot_http_finish);
}

void HttpManager::postHttpReq(QUrl url, QJsonObject json, ReqId req_id, Modules mod)
{
	QByteArray data = QJsonDocument(json).toJson();
	QNetworkRequest request(url);
	request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
	request.setHeader(QNetworkRequest::ContentLengthHeader, QByteArray::number(data.length()));

	QNetworkReply* reply = m_manager.post(request, data);
	QObject::connect(reply, &QNetworkReply::finished, [this, reply, req_id, mod]()
		{
			// 处理错误情况
			if (reply->error() != QNetworkReply::NoError)
			{
				qDebug() << reply->errorString();
				// 发送完成信号
				emit this->sig_http_finish(req_id, "", ErrorCodes::ERR_NETWORK, mod);
				return;
			}

			QString res = reply->readAll();
			// 发送完成信号
			emit this->sig_http_finish(req_id, res, ErrorCodes::SUCCESS, mod);

			reply->deleteLater();
			return;
		});
}

void HttpManager::slot_http_finish(ReqId id, QString res, ErrorCodes err, Modules mod)
{
	if (mod == Modules::REGISTERMOD)
	{
		// 发送信号通知注册模块http响应结束
		emit sig_reg_http_finish(id, res, err);
	}
	else if (mod == Modules::RESETMOD)
	{
		emit sig_reset_mod_finish(id, res, err);
	}
	else if (mod == Modules::LOGINMOD)
	{
		emit sig_login_mod_finish(id, res, err);
	}
}























