#include "logindialog.h"

#include "httpmanager.h"
#include "ui_logindialog.h"

#include <QRegularExpression>

#include "TCPManager.h"

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

	// 登录失败
	connect(&TCPManager::GetInstance(), &TCPManager::sig_loginFailed, this, &LoginDialog::slot_loginFailed);
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

	auto account = ui->account_edit->text().toInt();
	auto password = ui->pass_edit->text();

	QJsonObject json_object;
	json_object["user"] = account;
	json_object["password"] = password;
	HttpManager::GetInstance().postHttpReq(
		QUrl(GATE_URL_PERFIX + "/user_login"),
		json_object,
		ReqId::ID_LOGIN_USER,
		Modules::LOGIN_MOD);
}

void LoginDialog::initHttpHandlers()
{
	m_handlers.insert(ReqId::ID_LOGIN_USER, [this](QJsonObject json_object)
	{
		int error = json_object["error"].toInt();
		if (error != ErrorCodes::SUCCESS)
		{
			if (error == ErrorCodes::PASSWORD_ERROR)
			{
				showTip(tr("账号或密码错误"), false);
			}
			else
			{
				showTip(tr("参数错误"), false);
			}
			return;
		}
		auto user = json_object["user"].toString();

		ServerInfo si;
		si._uid = json_object["uid"].toInt();
		si._token = json_object["token"].toString();
		si._host = json_object["host"].toString();
		si._port = json_object["port"].toString();

		m_uid = si._uid;
		m_token = si._token;

		showTip(tr("登录成功"), true);
		qDebug() << "user account: " << user;
		qDebug() << "user is " << user << " uid is " << si._uid << " host is "
			<< si._host << " Port is " << si._port << " Token is " << si._token;
		emit sig_connectTcp(si);
	});
}

void LoginDialog::slot_login_mod_finish(ReqId id, QString res, ErrorCodes err)
{
	if (err != ErrorCodes::SUCCESS)
	{
		showTip(tr("网络请求错误"), false);
		return;
	}

	QJsonDocument json_document = QJsonDocument::fromJson(res.toUtf8());
	if (json_document.isEmpty() || !json_document.isObject())
	{
		showTip(tr("Json解析失败"), false);
		return;
	}

	m_handlers[id](json_document.object());
}

void LoginDialog::slot_tcpConnectFinish(bool success)
{
	if (success)
	{
		showTip(tr("服务器连接成功，正在登录..."), true);
		QJsonObject json_object;
		json_object["uid"] = m_uid;
		json_object["token"] = m_token;

		QJsonDocument document(json_object);
		QString json_str = document.toJson(QJsonDocument::Indented);

		// 发送tcp请求
		TCPManager::GetInstance().sig_sendData(ReqId::ID_CHAT_LOGIN, json_str);
	}
	else
	{
		showTip(tr("网络异常"), false);
	}
}

void LoginDialog::slot_loginFailed(int error_code)
{
	qDebug() << "[login] login to chat server error: " << error_code;
	showTip(tr("登录失败"), false);
}
