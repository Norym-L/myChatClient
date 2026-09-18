#pragma once
#include <QObject>
#include <qtcpsocket.h>
#include <QString>
#include <QByteArray>

#include "singleton.h"
#include "global.h"

class TCPManager : public QObject, public Singleton<TCPManager>
{
	Q_OBJECT

private:
	TCPManager();

	QTcpSocket m_socket;
	QString m_host;
	std::uint16_t m_port;
	QByteArray m_buffer;
	bool m_beReceivePending;
	quint16 m_messageId;
	quint16 m_messageLen;

public slots:
	void slot_tcpConnect(ServerInfo);
	void slot_sendData(ReqId req_id, QString data);
signals:
	void sig_connectSuccess(bool be_success);
	void sig_sendData(ReqId req_id, QString data);
};
