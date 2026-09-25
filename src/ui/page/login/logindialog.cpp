#include "ui/page/login/logindialog.h"

#include "network/httpmanager.h"
#include "ui_logindialog.h"

#include <QJsonDocument>
#include <QRegularExpression>

#include "network/TCPManager.h"
#include "data/UserManager.h"
#include "core/logger.h"

void LoginDialog::enableLoginButton(const bool enable) const
{
	ui->login_button->setEnabled(enable);
}


LoginDialog::LoginDialog(QWidget* parent)
	: QDialog(parent)
	  , ui(new Ui::LoginDialog)
{
	ui->setupUi(this);

	connect(ui->register_button, &QPushButton::clicked, this, &LoginDialog::switchRegister);

	ui->forget_label->setCursor(Qt::PointingHandCursor);
	ui->forget_label->setState("normal", "hover", "hover", "normal", "hover", "hover");
	connect(ui->forget_label, &ClickedLabel::clicked, this, [this]()
	{
		emit switchReset();
	});

	ui->error_tip->clear();

	/// 登录按钮
	connect(ui->login_button, &QPushButton::clicked, this, [this]()
	{
		login();
	});

	/// 错误提示
	connect(ui->pass_edit, &QLineEdit::editingFinished, this, [this]()
	{
		checkPasswordImputValid();
	});
	connect(ui->account_edit, &QLineEdit::editingFinished, this, [this]()
	{
		checkAccountImputValid();
	});

	/// 消息处理
	initHttpHandlers();
	connect(&HttpManager::GetInstance(), &HttpManager::sig_login_mod_finish, this, &LoginDialog::slot_login_mod_finish);

	/// tcp连接
	connect(this, &LoginDialog::sig_connectTcp, &TCPManager::GetInstance(), &TCPManager::slot_tcpConnect);
	connect(&TCPManager::GetInstance(), &TCPManager::sig_connectSuccess, this, &LoginDialog::slot_tcpConnectFinish);

	/// 聊天服登录结果
	connect(&TCPManager::GetInstance(), &TCPManager::sig_loginFailed, this, &LoginDialog::slot_loginFailed);
	connect(&TCPManager::GetInstance(), &TCPManager::sig_tcpError, this, &LoginDialog::slot_tcpError);
	connect(&TCPManager::GetInstance(), &TCPManager::sig_switchToChatDialog, this, &LoginDialog::slot_switchToChatDialog);
}

LoginDialog::~LoginDialog()
{
	delete ui;
}

bool LoginDialog::checkAccountImputValid()
{
	auto account = ui->account_edit->text();
	QRegularExpression regx(R"(^[1-9][0-9]{3,}$)");
	bool match = regx.match(account).hasMatch();
	if (!match)
	{
		addTipErr(TipErr::TIP_USER_ERR, "输入账号非法");
		return false;
	}

	delTipErr(TipErr::TIP_USER_ERR);
	return true;
}

bool LoginDialog::checkPasswordImputValid()
{
	auto pass = ui->pass_edit->text();
	if (pass.length() < 6 || pass.length() > 16)
	{
		addTipErr(TipErr::TIP_PWD_ERR, tr("密码长度应为 6~15 个字符"));
		return false;
	}

	QRegularExpression reg_exp(R"(^[a-zA-Z0-9!@#$%^&*]{6,15}$)");
	bool match = reg_exp.match(pass).hasMatch();
	if (!match)
	{
		addTipErr(TipErr::TIP_PWD_ERR, tr("密码不能包含非法字符"));
		return false;
	}

	delTipErr(TipErr::TIP_PWD_ERR);
	return true;
}

void LoginDialog::addTipErr(const TipErr tip, const QString& notice)
{
	m_tipErrs[tip] = notice;
	showTip(notice, false);
}

void LoginDialog::delTipErr(const TipErr tip)
{
	m_tipErrs.remove(tip);
	if (m_tipErrs.empty())
	{
		ui->error_tip->clear();
		return;
	}

	showTip(m_tipErrs.first(), false);
}

void LoginDialog::showTip(const QString& str, bool b_ok) const
{
	if (b_ok)
	{
		ui->error_tip->setProperty("state", "normal");
	}
	else
	{
		ui->error_tip->setProperty("state", "err");
	}
	ui->error_tip->setText(str);

	repolish(ui->error_tip);
}

void LoginDialog::login()
{
	if (!checkAccountImputValid() ||
		!checkPasswordImputValid())
	{
		return;
	}

	// 文档：/user_login 的 user 字段为 uid（数字）；email 仅用于响应回显、不参与校验
	const int uid = ui->account_edit->text().toInt();
	const QString password = ui->pass_edit->text();

	QJsonObject json_object;
	json_object.insert("user", uid);
	json_object.insert("password", password);

	showTip(tr("正在登录..."), true);
	enableLoginButton(false);

	HttpManager::GetInstance().postHttpReq(
		QUrl(GATE_URL_PREFIX + "/user_login"),
		json_object,
		ReqId::ID_LOGIN_USER,
		Modules::LOGIN_MOD);
}

void LoginDialog::initHttpHandlers()
{
	m_handlers.insert(ReqId::ID_LOGIN_USER, [this](const QJsonObject& json_object)
	{
		const int error = json_object.value("error").toInt(ErrorCodes::ERROR_JSON);
		if (error != ErrorCodes::SUCCESS)
		{
			// 文档：1006 凭据错误、1013 后端故障（稍后重试）、1002 获取聊天服失败
			showTip(errorCodeToTip(error), false);
			enableLoginButton(true);
			return;
		}

		ServerInfo si;
		si._uid = json_object.value("uid").toInt();
		si._token = json_object.value("token").toString();
		si._host = json_object.value("host").toString();
		si._port = json_object.value("port").toString();

		// 文档：响应中的 email 为请求回显（未传则为空串）
		const QString email = json_object.value("email").toString();

		if (si._token.isEmpty() || si._host.isEmpty() || si._port.isEmpty())
		{
			LOG_WARN() << "[login] login resp is incomplete:" << json_object;
			showTip(tr("服务器返回数据不完整"), false);
			enableLoginButton(true);
			return;
		}

		m_uid = si._uid;
		m_token = si._token;

		LOG_INFO() << "[login] http login success, uid:" << si._uid << "email:" << email
			<< "host:" << si._host << "port:" << si._port;

		// 文档：取得 token/host/port 后应连接该 ChatServer 并发送 message_id = 0 的登录帧
		showTip(tr("登录成功，正在连接聊天服务器..."), true);
		emit sig_connectTcp(si);
	});
}

void LoginDialog::slot_login_mod_finish(ReqId id, QString res, ErrorCodes err)
{
	if (err != ErrorCodes::SUCCESS)
	{
		showTip(errorCodeToTip(err), false);
		enableLoginButton(true);
		return;
	}

	QJsonDocument json_document = QJsonDocument::fromJson(res.toUtf8());
	if (json_document.isEmpty() || !json_document.isObject())
	{
		showTip(tr("服务器返回数据解析失败"), false);
		enableLoginButton(true);
		return;
	}

	const auto iter = m_handlers.find(id);
	if (iter == m_handlers.end())
	{
		LOG_WARN() << "[login] no handler for req id:" << static_cast<int>(id);
		enableLoginButton(true);
		return;
	}

	iter.value()(json_document.object());
}

void LoginDialog::slot_tcpConnectFinish(bool success)
{
	if (!success)
	{
		showTip(tr("连接聊天服务器失败，请稍后重试"), false);
		enableLoginButton(true);
		return;
	}

	showTip(tr("已连接聊天服务器，正在登录..."), true);

	// 文档：MESSAGE_CHAT_LOGIN(0) 的 body 为 {"uid": <int>, "token": <string>}
	QJsonObject json_object;
	json_object.insert("uid", m_uid);
	json_object.insert("token", m_token);

	if (!TCPManager::GetInstance().sendChatMessage(ReqId::ID_CHAT_LOGIN, json_object))
	{
		showTip(tr("登录消息发送失败"), false);
		enableLoginButton(true);
	}
}

void LoginDialog::slot_loginFailed(int error_code)
{
	LOG_WARN() << "[login] login to chat server error:" << error_code;
	showTip(errorCodeToTip(error_code), false);
	enableLoginButton(true);
}

void LoginDialog::slot_tcpError(const QString& reason)
{
	LOG_WARN() << "[login] chat server link error:" << reason;
	showTip(errorCodeToTip(ErrorCodes::ERR_NETWORK), false);
	enableLoginButton(true);
}

void LoginDialog::slot_switchToChatDialog()
{
	auto& user = UserManager::GetInstance();
	LOG_INFO() << "[login] chat server login success, uid:" << user.getUid() << "name:" << user.getName();

	// 文档：当前 ChatServer 仅支持登录消息，尚无聊天转发等业务消息类型
	showTip(tr("登录成功"), true);
	enableLoginButton(false);
	ui->account_edit->setEnabled(false);
	ui->pass_edit->setEnabled(false);

	emit sig_switchChatPage();
}
