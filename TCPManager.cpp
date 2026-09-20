#include "TCPManager.h"
#include "TCPManager.h"

#include "logindialog.h"
#include "UserManager.h"

void TCPManager::handleMessage(ReqId id, int len, QByteArray data)
{
	auto find_iter = m_handlers.find(id);
	if (find_iter == m_handlers.end())
	{
		qDebug() << "[tcp] message handle a unknow reqId: " << id;
	}

	find_iter->second(id, len, data);
}

TCPManager::TCPManager()
	: m_host(""), m_port(0), m_messageId(0), m_messageLen(0), m_beReceivePending(false)
{
	// 连接完成
	connect(&m_socket, &QTcpSocket::connected, [&]()
	{
		qDebug() << "[tcp] connected to server";
		emit sig_connectSuccess(true);
	});

	// 可以读取数据
	connect(&m_socket, &QTcpSocket::readyRead, [&]()
	{
		m_buffer.append(m_socket.readAll());

		QDataStream stream(&m_buffer, QIODevice::ReadOnly);
		stream.setVersion(QDataStream::Qt_5_0);

		static constexpr int message_head_len = sizeof(m_messageId) + sizeof(m_messageLen);

		forever
		{
			if (!m_beReceivePending)
			{
				// 检查缓冲区是否足够解析出消息头（消息id + 消息长度）
				if (m_buffer.size() < message_head_len)
				{
					return; // 数据不够，继续等待更多数据
				}

				// 读取消息并移除缓冲区
				stream >> m_messageId >> m_messageLen;
				m_buffer = m_buffer.mid(message_head_len);

				m_beReceivePending = true;
				qDebug() << "[tcp] receive message id: " << m_messageId << " length: " << m_messageLen;
			}

			if (m_buffer.size() < m_messageLen)
			{
				return; // 数据不够，继续等待更多数据
			}

			m_beReceivePending = false;
			QByteArray message_body = m_buffer.mid(0, m_messageLen);
			qDebug() << "[tcp] receive message body: " << message_body;

			m_buffer = m_buffer.mid(m_messageLen);
		}
	});

	// 错误处理
	connect(&m_socket,
	        QOverload<QAbstractSocket::SocketError>::of(&QTcpSocket::errorOccurred),
	        [&](QAbstractSocket::SocketError socketError)
	        {
		        Q_UNUSED(socketError)
		        qDebug() << "[Tcp] error: " << m_socket.errorString();
	        });
}

void TCPManager::initHandlers()
{
	m_handlers.emplace(ReqId::ID_CHAT_LOGIN_RESP, [this](ReqId id, int len, QByteArray data)
	{
		qDebug() << "[tcp] handle message id: " << id << " data is: " << data;
		QJsonDocument json_document = QJsonDocument::fromJson(data);

		if (json_document.isNull())
		{
			qDebug() << "[tcp] handle failed to parse json!";
			return;
		}

		QJsonObject json_object = json_document.object();
		if (!json_object.contains("error"))
		{
			int error = ErrorCodes::ERROR_JSON;
			qDebug() << "[tcp] handle login failed, error: " << error;
			emit sig_loginFailed(error);
			return;
		}

		int error = json_object["error"].toInt();
		if (error != ErrorCodes::SUCCESS)
		{
			qDebug() << "[tcp] handle login failed, error" << error;
			emit sig_loginFailed(error);
			return;
		}

		auto& user = UserManager::GetInstance();
		user.setUid(json_object["uid"].toInt());
		user.setName(json_object["name"].toString());
		user.setToken(json_object["token"].toString());
		emit sig_switchToChatDialog();
	});
}

void TCPManager::slot_tcpConnect(const ServerInfo& si)
{
	qDebug() << "[tcp] receive tcp connect signal";
	qDebug() << "[tcp] connect to server ...";
	m_host = si._host;
	m_port = static_cast<uint16_t>(si._port.toUInt());
	m_socket.connectToHost(m_host, m_port);
}

void TCPManager::slot_sendData(const ReqId req_id, const QString& data)
{
	QByteArray data_bytes = data.toUtf8();

	const quint16 len = static_cast<quint16>(data.size());

	QByteArray block; // 需要发送的数据
	QDataStream out(&block, QIODevice::WriteOnly);

	out.setByteOrder(QDataStream::BigEndian); // 设置网络字节序

	out << static_cast<uint16_t>(req_id) << len;
	block.append(data.toStdString().c_str());

	m_socket.write(block);
}
