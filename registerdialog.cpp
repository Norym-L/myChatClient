#include "registerdialog.h"
#include "ui_registerdialog.h"

#include "global.h"
#include "httpmanager.h"
#include "clicked_label.h"

bool RegisterDialog::checkUserNameImputValid()
{
	if (ui->user_name_edit->text().isEmpty())
	{
		addTipErr(TipErr::TIP_USER_ERR, tr("用户名不能为空"));
		return false;
	}

	delTipErr(TipErr::TIP_USER_ERR);
	return true;
}

bool RegisterDialog::checkEmailImputValid()
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

bool RegisterDialog::checkPasswordImputValid()
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

bool RegisterDialog::checkPassword2ImputValid()
{
	if (ui->pass_edit->text() != ui->pass2_edit->text())
	{
		addTipErr(TipErr::TIP_CONFIRM_ERR, tr("两次密码不相等"));
		return false;
	}

	delTipErr(TipErr::TIP_CONFIRM_ERR);
	return true;
}

bool RegisterDialog::checkVerifyImputValid()
{
	auto verify = ui->token_edit->text();
	if (verify.isEmpty())
	{
		addTipErr(TipErr::TIP_VARIFY_ERR, tr("验证码不能为空"));
		return false;
	}

	delTipErr(TipErr::TIP_VARIFY_ERR);
	return true;
}

RegisterDialog::RegisterDialog(QWidget* parent)
	: QDialog(parent)
	, ui(new Ui::RegisterDialog)
{
	ui->setupUi(this);
	ui->pass_edit->setEchoMode(QLineEdit::Password);
	ui->pass2_edit->setEchoMode(QLineEdit::Password);

	ui->err_tip->clear();

	connect(ui->user_name_edit, &QLineEdit::editingFinished, this, [this]()
		{
			checkUserNameImputValid();
		});

	connect(ui->email_edit, &QLineEdit::editingFinished, this, [this]()
		{
			checkEmailImputValid();
		});

	connect(ui->pass_edit, &QLineEdit::editingFinished, this, [this]()
		{
			checkPasswordImputValid();
		});

	connect(ui->pass2_edit, &QLineEdit::editingFinished, this, [this]()
		{
			checkPassword2ImputValid();
		});

	connect(ui->token_edit, &QLineEdit::editingFinished, this, [this]()
		{
			checkVerifyImputValid();
		});

	// 设置显示隐藏按钮
	ui->pass_visible->setCursor(Qt::PointingHandCursor);
	ui->pass2_visible->setCursor(Qt::PointingHandCursor);
	ui->pass_visible->setState("unvisible", "unvisible_hover", "visible_hover", "visible", "visible_hover", "unvisible_hover");
	ui->pass2_visible->setState("unvisible", "unvisible_hover", "visible_hover", "visible", "visible_hover", "unvisible_hover");

	connect(ui->pass_visible, &ClickedLabel::clicked, this, [this]()
		{
			ui->pass_edit->setEchoMode(QLineEdit::Normal);
			qDebug() << "passlabel was clicked";
		});

	connect(ui->pass_visible, &ClickedLabel::released, this, [this]()
		{
			ui->pass_edit->setEchoMode(QLineEdit::Password);
			qDebug() << "passlabel was released";
		});

	connect(ui->pass2_visible, &ClickedLabel::clicked, this, [this]()
		{
			ui->pass2_edit->setEchoMode(QLineEdit::Normal);
			qDebug() << "pass2label was clicked";
		});

	connect(ui->pass2_visible, &ClickedLabel::released, this, [this]()
		{
			ui->pass2_edit->setEchoMode(QLineEdit::Password);
			qDebug() << "pass2label was released";
		});


	connect(&HttpManager::GetInstance(), &HttpManager::sig_reg_http_finish, this, &RegisterDialog::slot_reg_mod_finish);

	// 注册回调函数
	initHttpHandlers();
}

RegisterDialog::~RegisterDialog()
{
	delete ui;
}

void RegisterDialog::addTipErr(const TipErr tip, const QString& notice)
{
	m_tipErrs[tip] = notice;
	showTip(notice, false);
}

void RegisterDialog::delTipErr(const TipErr tip)
{
	m_tipErrs.remove(tip);
	if (m_tipErrs.empty())
	{
		ui->err_tip->clear();
		return;
	}

	showTip(m_tipErrs.first(), false);
}

void RegisterDialog::on_get_token_button_clicked()
{
	auto email = ui->email_edit->text();
	QRegularExpression regex(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)");
	bool match = regex.match(email).hasMatch();
	if (match)
	{
		// 发送验证码
		QJsonObject json_obj;
		json_obj["email"] = email;
		HttpManager::GetInstance().postHttpReq(QUrl(GATE_URL_PERFIX + "/get_Verifycode"),
			json_obj,
			ReqId::ID_GET_VERIFY_CODE,
			Modules::REGISTERMOD);
	}
	else
	{
		showTip(tr("邮箱地址不准确"), false);
	}
}

void RegisterDialog::slot_reg_mod_finish(ReqId id, QString res, ErrorCodes err)
{
	if (err != ErrorCodes::SUCCESS)
	{
		showTip(tr("网络请求错误"), false);
		return;
	}

	// json解析
	QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
	if (jsonDoc.isNull())
	{
		showTip(tr("josn解析失败"), false);
		return;
	}
	if (!jsonDoc.isObject())
	{
		showTip(tr("json解析失败"), false);
		return;
	}

	m_handlers[id](jsonDoc.object());

	return;
}

void RegisterDialog::initHttpHandlers()
{
	// 获取验证码回调函数
	m_handlers.insert(ReqId::ID_GET_VERIFY_CODE, [this](const QJsonObject& jsonObj)
		{
			int error = jsonObj["error"].toInt();
			if (error != ErrorCodes::SUCCESS)
			{
				showTip(tr("参数错误"), false);
				qDebug() << "error: " << error << "  json: " << jsonObj << "\n";
				return;
			}

			auto email = jsonObj["email"].toString();
			showTip(tr("验证码已发送到邮箱，注意查收"), true);
			qDebug() << "email is: " << email;
		});

	// 注册用户回包逻辑
	m_handlers.insert(ReqId::ID_REG_USER, [this](const QJsonObject& jsonObj)
		{
			int error = jsonObj["error"].toInt();
			if (error != ErrorCodes::SUCCESS)
			{
				showTip(tr("参数错误"), false);
				return;
			}

			auto email = jsonObj["email"].toString();
			showTip(tr("注册成功"), true);
			qDebug() << "email is " << email;
		});
}

void RegisterDialog::showTip(const QString& str, bool b_ok)
{
	if (b_ok)
	{
		ui->err_tip->setProperty("state", "normal");
	}
	else
	{
		ui->err_tip->setProperty("state", "err");
	}
	ui->err_tip->setText(str);

	repolish(ui->err_tip);
}


void RegisterDialog::on_confirm_button_clicked()
{
	if (!checkEmailImputValid() ||
		!checkUserNameImputValid() ||
		!checkPasswordImputValid() ||
		!checkPassword2ImputValid() ||
		!checkVerifyImputValid())
	{
		return;
	}

	// //day11 发送http请求注册用户
	// QJsonObject json_obj;
	// json_obj["user"] = ui->user_name_edit->text();
	// json_obj["email"] = ui->email_edit->text();
	// json_obj["password"] = ui->pass_edit->text(); // 密码
	// json_obj["confirm"] = ui->pass2_edit->text(); // 密码2确认密码
	// json_obj["verify_code"] = ui->token_edit->text(); // 验证码
	// HttpManager::GetInstance().postHttpReq(QUrl(GATE_URL_PERFIX + "/user_register"),
	// 	json_obj,
	// 	ReqId::ID_REG_USER,
	// 	Modules::REGISTERMOD);
}

