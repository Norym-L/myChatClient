/**
 * @file protocol_conformance.cpp
 * @brief ChatClient 与 GateServer / ChatServer 接口协议一致性测试
 *
 * 依据 ServerDocs 下的接口文档，用本地 mock 服务端校验客户端实际发出的
 * 请求地址、请求方式、请求参数字段，以及客户端对响应字段和错误码的处理：
 *   - api-http.md     : 5 个 HTTP 接口、HTTP 200 + error 字段语义、404/405
 *   - api-tcp.md      : [message_id:2][body_len:2][body] 大端帧、登录消息
 *   - error-codes.md  : 1001~1013 错误码语义（含 1013 与 1006/1007 的区分）
 *
 * 全部用例通过时进程返回 0，否则返回 1。
 *
 * Windows 提示：本程序使用 QApplication，Qt 默认会把日志写到调试器输出，
 * 想在控制台看到结果需要先设置环境变量 QT_ASSUME_STDERR_HAS_CONSOLE=1。
 */

#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPushButton>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QtEndian>

#include <functional>

#include "TCPManager.h"
#include "UserManager.h"
#include "global.h"
#include "httpmanager.h"
#include "logindialog.h"
#include "registerdialog.h"
#include "resetdialog.h"

namespace
{
	int g_checks = 0;
	int g_failures = 0;
	QString g_section;

	void section(const QString& name)
	{
		g_section = name;
		qInfo().noquote() << "\n====" << name << "====";
	}

	void check(const bool be_pass, const QString& what)
	{
		++g_checks;
		if (be_pass)
		{
			qInfo().noquote() << "  [ OK ]" << what;
			return;
		}
		++g_failures;
		qWarning().noquote() << "  [FAIL]" << what;
	}

	void checkEqual(const QString& actual, const QString& expected, const QString& what)
	{
		check(actual == expected, QString("%1 => 期望 [%2]，实际 [%3]").arg(what, expected, actual));
	}

	void checkEqual(const int actual, const int expected, const QString& what)
	{
		check(actual == expected, QString("%1 => 期望 [%2]，实际 [%3]").arg(what).arg(expected).arg(actual));
	}

	QString toCompact(const QJsonObject& object)
	{
		return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
	}

	/// 等待条件成立；期间持续处理事件（网络响应都靠事件循环投递）
	bool waitFor(const std::function<bool()>& be_done, const int timeout_ms = 5000)
	{
		QElapsedTimer timer;
		timer.start();
		while (!be_done() && timer.elapsed() < timeout_ms)
		{
			QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
		}
		return be_done();
	}

	/**
	 * @brief MockGateServer GateServer 的最小 mock
	 * 按 api-http.md 返回 HTTP 200 + JSON（Content-Type: text/json，Server: GateServer），
	 * 并校验客户端请求的方法 / 路径 / Content-Type / 请求体
	 */
	class MockGateServer : public QTcpServer
	{
	public:
		struct Request
		{
			QString method;
			QString path;
			QString content_type;
			QByteArray body;
		};

		struct Response
		{
			int status = 200;
			QString content_type = "text/json";
			QByteArray body;
			bool close_connection = true;
		};

		explicit MockGateServer(QObject* parent = nullptr)
			: QTcpServer(parent)
		{
			// 文档：未知路径 404 url not found
			m_default_response.status = 404;
			m_default_response.content_type = "text/plain";
			m_default_response.body = "url not found";
		}

		/// 近 5 秒内收到的请求（每次用例前清空）
		QList<Request> requests;
		/// 按路径配置响应
		QHash<QString, Response> routes;

		void clearRequests()
		{
			requests.clear();
		}

	protected:
		void incomingConnection(const qintptr socket_descriptor) override
		{
			auto* socket = new QTcpSocket(this);
			socket->setSocketDescriptor(socket_descriptor);

			connect(socket, &QTcpSocket::readyRead, this, [this, socket]()
			{
				QByteArray& buffer = m_buffers[socket];
				buffer.append(socket->readAll());

				const int head_end = buffer.indexOf("\r\n\r\n");
				if (head_end < 0)
				{
					return;
				}

				const QByteArray head = buffer.left(head_end);
				const QList<QByteArray> lines = head.split('\n');

				int content_length = 0;
				Request request;
				const QList<QByteArray> first_line = lines.value(0).trimmed().split(' ');
				request.method = QString::fromLatin1(first_line.value(0));
				request.path = QString::fromLatin1(first_line.value(1));

				for (const QByteArray& line : lines)
				{
					const int colon = line.indexOf(':');
					if (colon < 0)
					{
						continue;
					}
					const QByteArray key = line.left(colon).trimmed().toLower();
					const QByteArray value = line.mid(colon + 1).trimmed();
					if (key == "content-type")
					{
						request.content_type = QString::fromLatin1(value);
					}
					else if (key == "content-length")
					{
						content_length = value.toInt();
					}
				}

				if (buffer.size() < head_end + 4 + content_length)
				{
					return; // body 未收全
				}

				request.body = buffer.mid(head_end + 4, content_length);
				m_buffers.remove(socket);
				requests.append(request);

				const Response response = routes.value(request.path, m_default_response);
				QByteArray out = "HTTP/1.1 " + QByteArray::number(response.status) + " status\r\n";
				out += "Content-Type: " + response.content_type.toLatin1() + "\r\n";
				out += "Server: GateServer\r\n";
				out += "Content-Length: " + QByteArray::number(response.body.size()) + "\r\n";
				out += "Connection: close\r\n\r\n";
				out += response.body;

				socket->write(out);
				socket->flush();
				socket->disconnectFromHost();
			});

			connect(socket, &QTcpSocket::disconnected, this, [this, socket]()
			{
				m_buffers.remove(socket);
				socket->deleteLater();
			});
		}

	private:
		QHash<QTcpSocket*, QByteArray> m_buffers;
		Response m_default_response;
	};

	/**
	 * @brief MockChatServer ChatServer 的最小 mock
	 * 按 api-tcp.md 解析 [message_id:2 大端][body_len:2 大端][body]，
	 * 并按脚本回包；可配置分块发送以验证半帧处理
	 */
	class MockChatServer : public QTcpServer
	{
	public:
		struct Frame
		{
			quint16 message_id = 0xFFFF;
			QByteArray body;
		};

		explicit MockChatServer(QObject* parent = nullptr)
			: QTcpServer(parent)
		{
			m_chunk_timer.setSingleShot(true);
			connect(&m_chunk_timer, &QTimer::timeout, this, [this]()
			{
				sendNextChunk();
			});
		}

		QList<Frame> frames;
		/// 收到每帧后按顺序回复的完整帧字节（空表示不回复）
		QList<QByteArray> reply_script;
		/// >0 时把每帧回复拆成该长度的多次 write，用于验证半帧 / 粘包处理
		int reply_chunk_size = 0;

		/// 按协议拼一个完整帧
		static QByteArray buildFrame(const quint16 message_id, const QByteArray& body)
		{
			QByteArray frame;
			frame.reserve(4 + body.size());
			frame.append(static_cast<char>((message_id >> 8) & 0xFF));
			frame.append(static_cast<char>(message_id & 0xFF));
			frame.append(static_cast<char>((body.size() >> 8) & 0xFF));
			frame.append(static_cast<char>(body.size() & 0xFF));
			frame.append(body);
			return frame;
		}

	protected:
		void incomingConnection(const qintptr socket_descriptor) override
		{
			auto* socket = new QTcpSocket(this);
			socket->setSocketDescriptor(socket_descriptor);

			connect(socket, &QTcpSocket::readyRead, this, [this, socket]()
			{
				QByteArray& buffer = m_buffers[socket];
				buffer.append(socket->readAll());

				for (;;)
				{
					if (buffer.size() < 4)
					{
						return;
					}

					const auto* head = reinterpret_cast<const uchar*>(buffer.constData());
					const quint16 message_id = qFromBigEndian<quint16>(head);
					const quint16 body_len = qFromBigEndian<quint16>(head + 2);
					if (buffer.size() < 4 + body_len)
					{
						return;
					}

					Frame frame;
					frame.message_id = message_id;
					frame.body = buffer.mid(4, body_len);
					buffer.remove(0, 4 + body_len);
					frames.append(frame);

					const QByteArray reply = reply_script.isEmpty() ? QByteArray() : reply_script.takeFirst();
					if (reply.isEmpty())
					{
						continue;
					}

					if (reply_chunk_size <= 0)
					{
						socket->write(reply);
						socket->flush();
						continue;
					}

					m_pending_reply = reply;
					m_pending_offset = 0;
					m_pending_socket = socket;
					sendNextChunk();
				}
			});

			connect(socket, &QTcpSocket::disconnected, this, [this, socket]()
			{
				m_buffers.remove(socket);
				if (m_pending_socket == socket)
				{
					m_pending_socket = nullptr;
				}
				socket->deleteLater();
			});
		}

	private:
		void sendNextChunk()
		{
			if (m_pending_socket == nullptr ||
				m_pending_socket->state() != QAbstractSocket::ConnectedState ||
				m_pending_offset >= m_pending_reply.size())
			{
				return;
			}

			const int chunk = qMin(reply_chunk_size, static_cast<int>(m_pending_reply.size()) - m_pending_offset);
			m_pending_socket->write(m_pending_reply.mid(m_pending_offset, chunk));
			m_pending_socket->flush();
			m_pending_offset += chunk;

			if (m_pending_offset < m_pending_reply.size())
			{
				m_chunk_timer.start(20); // 拆包发送，制造半帧
			}
		}

		QHash<QTcpSocket*, QByteArray> m_buffers;
		QByteArray m_pending_reply;
		int m_pending_offset = 0;
		QTcpSocket* m_pending_socket = nullptr;
		QTimer m_chunk_timer;
	};

	/// HTTP 调用结果
	struct HttpResult
	{
		bool be_done = false;
		ErrorCodes error = ErrorCodes::SUCCESS;
		QString body;
	};

	/**
	 * @brief sendHttp 通过 HttpManager 发起请求并等待所属模块的回包
	 */
	HttpResult sendHttp(const QString& url, const QJsonObject& request_body, const Modules mod)
	{
		auto result = std::make_shared<HttpResult>();

		using FinishHandler = void (*)(void*, ReqId, QString, ErrorCodes);
		QMetaObject::Connection connection;

		if (mod == Modules::LOGIN_MOD)
		{
			connection = QObject::connect(&HttpManager::GetInstance(), &HttpManager::sig_login_mod_finish,
				[result](ReqId, QString res, ErrorCodes err)
				{
					result->be_done = true;
					result->error = err;
					result->body = res;
				});
		}
		else if (mod == Modules::RESET_MOD)
		{
			connection = QObject::connect(&HttpManager::GetInstance(), &HttpManager::sig_reset_mod_finish,
				[result](ReqId, QString res, ErrorCodes err)
				{
					result->be_done = true;
					result->error = err;
					result->body = res;
				});
		}
		else
		{
			connection = QObject::connect(&HttpManager::GetInstance(), &HttpManager::sig_reg_http_finish,
				[result](ReqId, QString res, ErrorCodes err)
				{
					result->be_done = true;
					result->error = err;
					result->body = res;
				});
		}

		HttpManager::GetInstance().postHttpReq(QUrl(url), request_body, ReqId::ID_GET_VERIFY_CODE, mod);
		waitFor([result]() { return result->be_done; });
		QObject::disconnect(connection);

		return *result;
	}

	QString baseUrl(const MockGateServer& server)
	{
		return QString("http://127.0.0.1:%1").arg(server.serverPort());
	}

	QLineEdit* edit(QWidget* widget, const char* name)
	{
		return widget->findChild<QLineEdit*>(QString::fromLatin1(name));
	}

	QPushButton* button(QWidget* widget, const char* name)
	{
		return widget->findChild<QPushButton*>(QString::fromLatin1(name));
	}

	QString tipText(QWidget* widget, const char* name)
	{
		auto* label = widget->findChild<QLabel*>(QString::fromLatin1(name));
		return label == nullptr ? QString() : label->text();
	}

	// ---------------------------------------------------------------- HTTP 用例

	void testHttpRequestShapes(MockGateServer& server)
	{
		server.clearRequests();

		section("HTTP 请求地址 / 方式 / 参数（api-http.md）");

		// POST /get_Verifycode：必填 email
		{
			server.routes.insert("/get_Verifycode",
				{200, "text/json", R"({"error":0,"email":"user@example.com"})", true});
			QJsonObject request_body;
			request_body.insert("email", "user@example.com");
			const HttpResult result = sendHttp(baseUrl(server) + "/get_Verifycode", request_body, Modules::REGISTER_MOD);

			check(result.be_done && result.error == ErrorCodes::SUCCESS, "POST /get_Verifycode 返回 SUCCESS");
			const auto& request = server.requests.last();
			checkEqual(request.method, "POST", "请求方式");
			checkEqual(request.path, "/get_Verifycode", "请求路径");
			checkEqual(request.content_type, "application/json", "Content-Type");
			const QJsonObject sent = QJsonDocument::fromJson(request.body).object();
			check(sent.keys() == QStringList{"email"}, "请求体字段");
			checkEqual(sent.value("email").toString(), "user@example.com", "email 取值");
		}

		// POST /user_register：user 为昵称（字符串），需要 password / confirm / verify_code
		{
			server.routes.insert("/user_register",
				{200, "text/json", R"({"error":0,"uid":10001,"email":"user@example.com","user":"norym"})", true});
			QJsonObject request_body;
			request_body.insert("email", "user@example.com");
			request_body.insert("user", "norym");
			request_body.insert("password", "P@ssw0rd");
			request_body.insert("confirm", "P@ssw0rd");
			request_body.insert("verify_code", "123456");
			const HttpResult result = sendHttp(baseUrl(server) + "/user_register", request_body, Modules::REGISTER_MOD);

			check(result.be_done && result.error == ErrorCodes::SUCCESS, "POST /user_register 返回 SUCCESS");
			const auto& request = server.requests.last();
			checkEqual(request.method, "POST", "请求方式");
			checkEqual(request.path, "/user_register", "请求路径");
			const QJsonObject sent = QJsonDocument::fromJson(request.body).object();
			checkEqual(sent.keys().join(','), "confirm,email,password,user,verify_code", "请求体字段");
			check(sent.value("user").isString(), "user 为昵称（字符串）");
			checkEqual(sent.value("user").toString(), "norym", "user 取值");

			const QJsonObject response = QJsonDocument::fromJson(result.body.toUtf8()).object();
			check(!response.contains("password"), "响应不回显 password（B1）");
			checkEqual(response.value("uid").toInt(), 10001, "响应 uid 字段");
		}

		// POST /reset_password：user 为 uid（数字），验证码字段为 verify_code
		{
			server.routes.insert("/reset_password",
				{200, "text/json", R"({"error":0,"email":"user@example.com","verify_code":"123456"})", true});
			QJsonObject request_body;
			request_body.insert("user", 10001);
			request_body.insert("email", "user@example.com");
			request_body.insert("password", "NewP@ss");
			request_body.insert("verify_code", "123456");
			const HttpResult result = sendHttp(baseUrl(server) + "/reset_password", request_body, Modules::RESET_MOD);

			check(result.be_done && result.error == ErrorCodes::SUCCESS, "POST /reset_password 返回 SUCCESS");
			const auto& request = server.requests.last();
			checkEqual(request.method, "POST", "请求方式");
			checkEqual(request.path, "/reset_password", "请求路径");
			const QJsonObject sent = QJsonDocument::fromJson(request.body).object();
			checkEqual(sent.keys().join(','), "email,password,user,verify_code", "请求体字段");
			check(sent.value("user").isDouble(), "user 为 uid（数字）");
			check(!sent.contains("varifycode"), "不再使用旧字段 varifycode（B2）");

			const QJsonObject response = QJsonDocument::fromJson(result.body.toUtf8()).object();
			check(response.contains("verify_code"), "响应使用 verify_code（B2）");
		}

		// POST /user_login：user 为 uid，可带 email（仅回显）
		{
			server.routes.insert("/user_login",
				{200, "text/json",
				 R"({"error":0,"email":"","uid":10001,"token":"1f0c-abc","host":"127.0.0.1","port":"8090"})", true});
			QJsonObject request_body;
			request_body.insert("user", 10001);
			request_body.insert("password", "P@ssw0rd");
			const HttpResult result = sendHttp(baseUrl(server) + "/user_login", request_body, Modules::LOGIN_MOD);

			check(result.be_done && result.error == ErrorCodes::SUCCESS, "POST /user_login 返回 SUCCESS");
			const auto& request = server.requests.last();
			checkEqual(request.method, "POST", "请求方式");
			checkEqual(request.path, "/user_login", "请求路径");
			const QJsonObject sent = QJsonDocument::fromJson(request.body).object();
			checkEqual(sent.keys().join(','), "password,user", "请求体字段");
			check(sent.value("user").isDouble(), "user 为 uid（数字）");

			const QJsonObject response = QJsonDocument::fromJson(result.body.toUtf8()).object();
			checkEqual(response.value("token").toString(), "1f0c-abc", "响应 token");
			checkEqual(response.value("host").toString(), "127.0.0.1", "响应 host");
			checkEqual(response.value("port").toString(), "8090", "响应 port（字符串）");
		}
	}

	void testHttpStatusAndNetwork(MockGateServer& server)
	{
		server.clearRequests();

		section("HTTP 非 200 与网络异常（api-http.md 第 1 节）");

		// 文档：未知路径 404 + text/plain，正文 url not found
		{
			server.routes.clear();
			const HttpResult result = sendHttp(baseUrl(server) + "/not_found", QJsonObject(), Modules::RESET_MOD);
			check(result.be_done && result.error == ErrorCodes::ERR_HTTP_STATUS, "404 映射为 ERR_HTTP_STATUS");
		}

		// 文档：不支持的方法 405 method not allowed（当前客户端只用 POST，这里只验证状态码分支）
		{
			server.routes.clear();
			MockGateServer::Response response;
			response.status = 405;
			response.content_type = "text/plain";
			response.body = "method not allowed";
			server.routes.insert("/user_login", response);
			const HttpResult result = sendHttp(baseUrl(server) + "/user_login", QJsonObject(), Modules::LOGIN_MOD);
			check(result.be_done && result.error == ErrorCodes::ERR_HTTP_STATUS, "405 映射为 ERR_HTTP_STATUS");
		}

		// 连接失败（端口关闭）应映射为 ERR_NETWORK，而不是 ERR_HTTP_STATUS
		{
			quint16 closed_port = 0;
			{
				MockGateServer temp;
				temp.listen(QHostAddress::LocalHost, 0);
				closed_port = temp.serverPort();
			}
			const HttpResult result = sendHttp(QString("http://127.0.0.1:%1/user_login").arg(closed_port),
				QJsonObject(), Modules::LOGIN_MOD);
			check(result.be_done && result.error == ErrorCodes::ERR_NETWORK, "连接失败映射为 ERR_NETWORK");
		}
	}

	// ---------------------------------------------------------------- TCP 用例

	void testTcpFrameProtocol(MockChatServer& server)
	{
		section("ChatServer 帧协议（api-tcp.md）");

		server.frames.clear();
		server.reply_script.clear();
		server.reply_chunk_size = 0;

		// 文档 2.2：成功响应为多行 JSON，非 ASCII 可能转义为 \uXXXX
		const QByteArray login_resp =
			R"({
 "error" : 0,
 "uid" : 10001,
 "token" : "1f0c-abc",
 "name" : "\u5c0f\u660e"
})";
		server.reply_script.append(MockChatServer::buildFrame(ChatProtocol::MESSAGE_CHAT_LOGIN_RESP, login_resp));

		bool be_connected = false;
		bool be_login_ok = false;
		int failed_error = -1;

		const QMetaObject::Connection c1 = QObject::connect(&TCPManager::GetInstance(), &TCPManager::sig_connectSuccess,
			[&be_connected](bool success) { be_connected = success; });
		const QMetaObject::Connection c2 = QObject::connect(&TCPManager::GetInstance(), &TCPManager::sig_switchToChatDialog,
			[&be_login_ok]() { be_login_ok = true; });
		const QMetaObject::Connection c3 = QObject::connect(&TCPManager::GetInstance(), &TCPManager::sig_loginFailed,
			[&failed_error](int error) { failed_error = error; });

		ServerInfo server_info;
		server_info._uid = 10001;
		server_info._token = "1f0c-abc";
		server_info._host = "127.0.0.1";
		server_info._port = QString::number(server.serverPort());
		TCPManager::GetInstance().slot_tcpConnect(server_info);

		check(waitFor([&be_connected]() { return be_connected; }), "连接 /user_login 分配到的 ChatServer");

		QJsonObject login_body;
		login_body.insert("uid", 10001);
		login_body.insert("token", "1f0c-abc");
		check(TCPManager::GetInstance().sendChatMessage(ReqId::ID_CHAT_LOGIN, login_body),
			"发送 message_id = 0 的登录帧");

		check(waitFor([&server]() { return !server.frames.isEmpty(); }), "服务端收到登录帧");
		if (!server.frames.isEmpty())
		{
			const auto& frame = server.frames.first();
			checkEqual(static_cast<int>(frame.message_id), 0, "线上 message_id 为 0（不是 ReqId 1005）");
			const QJsonObject sent = QJsonDocument::fromJson(frame.body).object();
			checkEqual(sent.keys().join(','), "token,uid", "登录帧 body 字段");
			checkEqual(sent.value("uid").toInt(), 10001, "登录帧 uid");
			checkEqual(sent.value("token").toString(), "1f0c-abc", "登录帧 token");
		}

		check(waitFor([&be_login_ok]() { return be_login_ok; }), "解析 message_id = 1 的响应并进入登录成功分支");
		checkEqual(failed_error, -1, "未触发登录失败分支");
		checkEqual(UserManager::GetInstance().getUid(), 10001, "UserManager.uid");
		checkEqual(UserManager::GetInstance().getName(), QString::fromUtf8("小明"), "UserManager.name（\\uXXXX 转义）");
		checkEqual(UserManager::GetInstance().getToken(), "1f0c-abc", "UserManager.token");

		QObject::disconnect(c1);
		QObject::disconnect(c2);
		QObject::disconnect(c3);
	}

	void testTcpSplitFrame(MockChatServer& server)
	{
		section("TCP 半帧 / 粘包（api-tcp.md 帧格式）");

		server.frames.clear();
		server.reply_script.clear();
		server.reply_chunk_size = 5; // 每 5 字节发一次，必然出现半帧

		server.reply_script.append(MockChatServer::buildFrame(ChatProtocol::MESSAGE_CHAT_LOGIN_RESP,
			R"({"error":0,"uid":10001,"token":"tok","name":"norym"})"));

		bool be_connected = false;
		bool be_login_ok = false;
		const QMetaObject::Connection c1 = QObject::connect(&TCPManager::GetInstance(), &TCPManager::sig_connectSuccess,
			[&be_connected](bool success) { be_connected = success; });
		const QMetaObject::Connection c2 = QObject::connect(&TCPManager::GetInstance(), &TCPManager::sig_switchToChatDialog,
			[&be_login_ok]() { be_login_ok = true; });

		ServerInfo server_info;
		server_info._uid = 10001;
		server_info._token = "tok";
		server_info._host = "127.0.0.1";
		server_info._port = QString::number(server.serverPort());
		TCPManager::GetInstance().slot_tcpConnect(server_info);
		check(waitFor([&be_connected]() { return be_connected; }), "重新连接到 ChatServer");

		QJsonObject login_body;
		login_body.insert("uid", 10001);
		login_body.insert("token", "tok");
		TCPManager::GetInstance().sendChatMessage(ReqId::ID_CHAT_LOGIN, login_body);

		check(waitFor([&be_login_ok]() { return be_login_ok; }), "分 5 字节多次到达的响应帧被正确重组");

		QObject::disconnect(c1);
		QObject::disconnect(c2);
	}

	void testTcpErrorAndIllegalFrame(MockChatServer& server)
	{
		section("TCP 错误码与非法报文（error-codes.md / api-tcp.md 第 3 节）");

		// 1012 token 与服务端记录不一致
		{
			server.frames.clear();
			server.reply_script.clear();
			server.reply_chunk_size = 0;
			server.reply_script.append(MockChatServer::buildFrame(ChatProtocol::MESSAGE_CHAT_LOGIN_RESP,
				R"({"error":1012})"));

			bool be_connected = false;
			int failed_error = -1;
			const QMetaObject::Connection c1 = QObject::connect(&TCPManager::GetInstance(), &TCPManager::sig_connectSuccess,
				[&be_connected](bool success) { be_connected = success; });
			const QMetaObject::Connection c2 = QObject::connect(&TCPManager::GetInstance(), &TCPManager::sig_loginFailed,
				[&failed_error](int error) { failed_error = error; });

			ServerInfo server_info;
			server_info._uid = 10001;
			server_info._token = "bad";
			server_info._host = "127.0.0.1";
			server_info._port = QString::number(server.serverPort());
			TCPManager::GetInstance().slot_tcpConnect(server_info);
			waitFor([&be_connected]() { return be_connected; });

			QJsonObject login_body;
			login_body.insert("uid", 10001);
			login_body.insert("token", "bad");
			TCPManager::GetInstance().sendChatMessage(ReqId::ID_CHAT_LOGIN, login_body);

			check(waitFor([&failed_error]() { return failed_error != -1; }), "失败响应仅含 error 时进入失败分支");
			checkEqual(failed_error, ErrorCodes::USER_TOKEN_INVALID, "error = 1012 原样上报");
			checkEqual(errorCodeToTip(failed_error), QString::fromUtf8("登录凭据已失效，请重新登录"), "1012 提示文案");

			QObject::disconnect(c1);
			QObject::disconnect(c2);
		}

		// 非法帧头：message_id > MAX_LENGTH，客户端应关闭会话并上报链路异常
		{
			server.frames.clear();
			server.reply_script.clear();
			server.reply_script.append(MockChatServer::buildFrame(3000, "{}"));

			bool be_connected = false;
			bool be_link_error = false;
			const QMetaObject::Connection c1 = QObject::connect(&TCPManager::GetInstance(), &TCPManager::sig_connectSuccess,
				[&be_connected](bool success) { be_connected = success; });
			const QMetaObject::Connection c2 = QObject::connect(&TCPManager::GetInstance(), &TCPManager::sig_tcpError,
				[&be_link_error](const QString&) { be_link_error = true; });

			ServerInfo server_info;
			server_info._uid = 10001;
			server_info._token = "tok";
			server_info._host = "127.0.0.1";
			server_info._port = QString::number(server.serverPort());
			TCPManager::GetInstance().slot_tcpConnect(server_info);
			waitFor([&be_connected]() { return be_connected; });

			QJsonObject login_body;
			login_body.insert("uid", 10001);
			login_body.insert("token", "tok");
			TCPManager::GetInstance().sendChatMessage(ReqId::ID_CHAT_LOGIN, login_body);

			check(waitFor([&be_link_error]() { return be_link_error; }), "非法帧头上报 sig_tcpError 并关闭会话");
			check(!TCPManager::GetInstance().isConnected(), "非法帧头后连接已关闭");

			QObject::disconnect(c1);
			QObject::disconnect(c2);
		}
	}

	// ---------------------------------------------------------------- 端到端用例

	void testLoginDialogEndToEnd(MockGateServer& gate, MockChatServer& chat)
	{
		section("登录全链路（/user_login -> ChatServer 登录帧 -> 登录成功）");

		gate.clearRequests();
		gate.routes.clear();
		gate.routes.insert("/user_login",
			{200, "text/json",
			 QString(R"({"error":0,"email":"e2e@example.com","uid":20002,"token":"e2e-token","host":"127.0.0.1","port":"%1"})")
			 .arg(chat.serverPort()).toUtf8(),
			 true});

		chat.frames.clear();
		chat.reply_script.clear();
		chat.reply_chunk_size = 0;
		chat.reply_script.append(MockChatServer::buildFrame(ChatProtocol::MESSAGE_CHAT_LOGIN_RESP,
			R"({"error":0,"uid":20002,"token":"e2e-token","name":"e2e_user"})"));

		// 清掉前面 TCP 用例的残留，避免用旧值误判
		UserManager::GetInstance().setUid(0);
		UserManager::GetInstance().setName(QString());
		UserManager::GetInstance().setToken(QString());

		GATE_URL_PREFIX = baseUrl(gate);

		auto* dialog = new LoginDialog();
		edit(dialog, "account_edit")->setText("20002");
		edit(dialog, "pass_edit")->setText("P@ssw0rd");
		button(dialog, "login_button")->click();

		// 页面停留在「登录成功」说明 HTTP 登录 -> 连接 ChatServer -> 登录帧 -> 响应解析 全部走通
		check(waitFor([dialog]()
			{
				return tipText(dialog, "error_tip") == QString::fromUtf8("登录成功");
			}), "登录成功后页面提示切换为「登录成功」");

		check(!chat.frames.isEmpty(), "登录后自动连上 ChatServer 并发出登录帧");
		checkEqual(UserManager::GetInstance().getUid(), 20002, "UserManager.uid");
		checkEqual(UserManager::GetInstance().getName(), "e2e_user", "UserManager.name");
		checkEqual(UserManager::GetInstance().getToken(), "e2e-token", "UserManager.token");

		if (!gate.requests.isEmpty())
		{
			const auto& request = gate.requests.last();
			checkEqual(request.path, "/user_login", "请求路径");
			const QJsonObject sent = QJsonDocument::fromJson(request.body).object();
			checkEqual(sent.value("user").toInt(), 20002, "请求体 user 为 uid");
		}
		else
		{
			check(false, "登录请求已发出");
		}

		delete dialog;
	}

	void testLoginDialogErrorTips(MockGateServer& gate)
	{
		section("登录失败错误码文案（error-codes.md 第 2 节）");

		struct Case
		{
			int error;
			QString tip;
		};

		// 文档：1006 / 1007 为凭据错误，1013 为后端故障（应提示稍后重试）
		const QList<Case> cases{
			{1006, QString::fromUtf8("账号或密码错误")},
			{1013, QString::fromUtf8("服务器暂时不可用，请稍后重试")},
			{1002, QString::fromUtf8("服务器繁忙，请稍后重试")},
		};

		for (const Case& item : cases)
		{
			gate.clearRequests();
			gate.routes.clear();
			gate.routes.insert("/user_login",
				{200, "text/json", QString(R"({"error":%1})").arg(item.error).toUtf8(), true});

			GATE_URL_PREFIX = baseUrl(gate);

			auto* dialog = new LoginDialog();
			edit(dialog, "account_edit")->setText("10001");
			edit(dialog, "pass_edit")->setText("P@ssw0rd");
			button(dialog, "login_button")->click();

			const bool be_tipped = waitFor([dialog, &item]() { return tipText(dialog, "error_tip") == item.tip; });
			check(be_tipped, QString("error = %1 的提示文案").arg(item.error));
			check(button(dialog, "login_button")->isEnabled(), "失败后登录按钮恢复可用");

			delete dialog;
		}
	}

	void testRegisterAndResetDialogRequests(MockGateServer& gate)
	{
		section("注册 / 重置密码页的请求参数");

		gate.clearRequests();
		gate.routes.clear();
		gate.routes.insert("/user_register",
			{200, "text/json", R"({"error":0,"uid":10001,"email":"user@example.com","user":"norym"})", true});
		gate.routes.insert("/reset_password",
			{200, "text/json", R"({"error":0,"email":"user@example.com","verify_code":"123456"})", true});

		GATE_URL_PREFIX = baseUrl(gate);

		// 注册页：user 为昵称字符串
		{
			auto* dialog = new RegisterDialog();
			edit(dialog, "user_name_edit")->setText("norym");
			edit(dialog, "email_edit")->setText("user@example.com");
			edit(dialog, "pass_edit")->setText("P@ssw0rd");
			edit(dialog, "pass2_edit")->setText("P@ssw0rd");
			edit(dialog, "token_edit")->setText("123456");
			button(dialog, "confirm_button")->click();

			check(waitFor([&gate]() { return !gate.requests.isEmpty(); }), "注册请求已发出");
			if (!gate.requests.isEmpty())
			{
				const auto& request = gate.requests.last();
				checkEqual(request.path, "/user_register", "请求路径");
				const QJsonObject sent = QJsonDocument::fromJson(request.body).object();
				checkEqual(sent.value("user").toString(), "norym", "user 为昵称");
				checkEqual(sent.value("confirm").toString(), "P@ssw0rd", "confirm 字段");
				checkEqual(sent.value("verify_code").toString(), "123456", "verify_code 字段");
			}

			delete dialog;
		}

		// 重置密码页：user 为 uid，验证码字段为 verify_code
		{
			gate.clearRequests();
			auto* dialog = new ResetDialog();
			edit(dialog, "user_edit")->setText("10001");
			edit(dialog, "email_edit")->setText("user@example.com");
			edit(dialog, "new_password_edit")->setText("NewP@ss");
			edit(dialog, "verify_edit")->setText("123456");
			button(dialog, "ok_button")->click();

			check(waitFor([&gate]() { return !gate.requests.isEmpty(); }), "重置密码请求已发出");
			if (!gate.requests.isEmpty())
			{
				const auto& request = gate.requests.last();
				checkEqual(request.path, "/reset_password", "请求路径");
				const QJsonObject sent = QJsonDocument::fromJson(request.body).object();
				checkEqual(sent.keys().join(','), "email,password,user,verify_code", "请求体字段");
				checkEqual(sent.value("user").toInt(), 10001, "user 为 uid");
			}

			delete dialog;
		}
	}
} // namespace

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);

	MockGateServer gate_server;
	if (!gate_server.listen(QHostAddress::LocalHost, 0))
	{
		qWarning() << "[test] mock GateServer listen failed";
		return 1;
	}

	MockChatServer chat_server;
	if (!chat_server.listen(QHostAddress::LocalHost, 0))
	{
		qWarning() << "[test] mock ChatServer listen failed";
		return 1;
	}

	qInfo().noquote() << "mock GateServer port:" << gate_server.serverPort()
		<< " mock ChatServer port:" << chat_server.serverPort();

	testHttpRequestShapes(gate_server);
	testHttpStatusAndNetwork(gate_server);
	testTcpFrameProtocol(chat_server);
	testTcpSplitFrame(chat_server);
	testTcpErrorAndIllegalFrame(chat_server);
	testLoginDialogEndToEnd(gate_server, chat_server);
	testLoginDialogErrorTips(gate_server);
	testRegisterAndResetDialogRequests(gate_server);

	qInfo().noquote() << "\n==== 汇总 ====";
	qInfo().noquote() << QString("共 %1 项断言，失败 %2 项").arg(g_checks).arg(g_failures);
	return g_failures == 0 ? 0 : 1;
}
