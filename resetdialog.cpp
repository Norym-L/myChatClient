#include "resetdialog.h"
#include "resetdialog.h"

#include "httpmanager.h"

ResetDialog::ResetDialog(QWidget* parent)
	: QDialog(parent)
	, ui(new Ui::ResetDialogClass)
{
	ui->setupUi(this);

	connect(ui->email_edit, &QLineEdit::editingFinished, this, [this]()
		{
			checkEmailImputValid();
		});
	connect(ui->new_password_edit, &QLineEdit::editingFinished, this, [this]()
		{
			checkPasswordImputValid();
		});
	connect(ui->verify_edit, &QLineEdit::editingFinished, this, [this]()
		{
			checkVerifyImputValid();
		});

	initHandlers();
	connect(&HttpManager::GetInstance(), &HttpManager::sig_reset_mod_finish, this, &ResetDialog::slot_resetModFinish);

	// 取消按钮返回登录页
	connect(ui->cancel_button, &QPushButton::clicked, this, [this]()
		{
			emit sigSwitchLoginPage();
		});

	// 发送验证码
	connect(ui->get_verify_button, &QPushButton::clicked, this, [this]()
		{
			if (!checkEmailImputValid())
			{
				return;
			}

			QJsonObject json_object;
			json_object["email"] = ui->email_edit->text();
			HttpManager::GetInstance().postHttpReq(
				QUrl(GATE_URL_PERFIX + "/get_Verifycode"),
				json_object,
				ReqId::ID_GET_VERIFY_CODE,
				Modules::REGISTERMOD);
		});

	// 确认按钮
	connect(ui->ok_button, &QPushButton::clicked, this, [this]()
		{
			if (!checkEmailImputValid() ||
				!checkPasswordImputValid() ||
				!checkVerifyImputValid() ||
				!checkAccountImputValid())
			{
				return;
			}

			QJsonObject json_object;
			json_object["user"] = ui->user_edit->text().toInt();
			json_object["email"] = ui->email_edit->text();
			json_object["password"] = ui->new_password_edit->text();
			json_object["verify_code"] = ui->verify_edit->text();
			HttpManager::GetInstance().postHttpReq(
				QUrl(GATE_URL_PERFIX + "/reset_password"),
				json_object,
				ReqId::ID_RESET_PWD,
				Modules::RESETMOD);
		});
}

void ResetDialog::addTipErr(const TipErr tip, const QString& notice)
{
	m_tipErrs[tip] = notice;
	showTip(notice, false);
}

void ResetDialog::delTipErr(const TipErr tip)
{
	m_tipErrs.remove(tip);
	if (m_tipErrs.empty())
	{
		ui->error_tip->clear();
		return;
	}

	showTip(m_tipErrs.first(), false);
}

bool ResetDialog::checkAccountImputValid()
{
	auto account = ui->user_edit->text();
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

bool ResetDialog::checkEmailImputValid()
{
	auto email = ui->email_edit->text();
	QRegularExpression regex(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)");
	bool match = regex.match(email).hasMatch();
	if (!match)
	{
		addTipErr(TipErr::TIP_EMAIL_ERR, "邮箱地址不正确");
		return false;
	}

	delTipErr(TipErr::TIP_EMAIL_ERR);
	return true;
}

bool ResetDialog::checkPasswordImputValid()
{
	auto pass = ui->new_password_edit->text();
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

bool ResetDialog::checkVerifyImputValid()
{
	auto verify = ui->verify_edit->text();
	if (verify.isEmpty())
	{
		addTipErr(TipErr::TIP_VARIFY_ERR, tr("验证码不能为空"));
		return false;
	}

	delTipErr(TipErr::TIP_VARIFY_ERR);
	return true;
}

void ResetDialog::showTip(const QString& str, bool b_ok)
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

void ResetDialog::initHandlers()
{
	m_handlers.insert(ReqId::ID_GET_VERIFY_CODE, [this](QJsonObject json_object)
		{
			int error = json_object["error"].toInt();
			if (error != ErrorCodes::SUCCESS)
			{
				showTip(tr("参数错误"), false);
				return;
			}

			showTip(tr("验证码已经发送到邮箱，注意查收"), true);
			qDebug() << "email is: " << json_object["email"].toString();
		});

	m_handlers.insert(ReqId::ID_RESET_PWD, [this](QJsonObject json_object)
		{
			int error = json_object["error"].toInt();
			if (error != ErrorCodes::SUCCESS)
			{
				showTip(tr("参数错误"), false);
				return;
			}

			showTip(tr("重置成功，点击返回登录"), true);
			qDebug() << "email is: " << json_object["email"].toString();
			qDebug() << "uuid is:" << json_object["uuid"].toString();
		});
}

void ResetDialog::slot_resetModFinish(ReqId id, QString res, ErrorCodes error)
{
	if (error != ErrorCodes::SUCCESS)
	{
		showTip(tr("网络请求错误"), false);
		return;
	}

	QJsonDocument json_document = QJsonDocument::fromJson(res.toUtf8());
	if (json_document.isNull() ||
		!json_document.isObject())
	{
		showTip(tr("Json解析错误"), false);
		return;
	}

	m_handlers[id](json_document.object());
}

ResetDialog::~ResetDialog()
{}

