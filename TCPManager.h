#pragma once
#include <QObject>
#include <qtcpsocket.h>
#include <QString>
#include <QByteArray>

#include "singleton.h"
#include "global.h"

class TCPManager : public QObject, public Singleton<TCPManager>
{
	friend class Singleton;

	Q_OBJECT
public:
	void handleMessage(ReqId id, int len, QByteArray data);

private:
	TCPManager();

	void initHandlers();

	std::map<ReqId, std::function<void(ReqId , int , QByteArray)>> m_handlers;
	QByteArray m_buffer;
	QTcpSocket m_socket;
	QString m_host;
	std::uint16_t m_port;
	quint16 m_messageId;
	quint16 m_messageLen;
	bool m_beReceivePending;

public slots:
	void slot_tcpConnect(const ServerInfo&);
	void slot_sendData(ReqId req_id, const QString& data);
signals:
	void sig_connectSuccess(bool be_success);
	void sig_sendData(ReqId req_id, QString data);
	void sig_loginFailed(int error);
	void sig_switchToChatDialog(void);
};
