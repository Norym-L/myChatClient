#include "ui/page/register/registerdialog.h"
#include "ui_registerdialog.h"

#include <QJsonDocument>

#include "app/global.h"
#include "network/httpmanager.h"
#include "ui/components/clicked_label.h"
#include "core/logger.h"

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
		addTipErr(TipErr::TIP_VERIFY_ERR, tr("验证码不能为空"));
		return false;
	}

	delTipErr(TipErr::TIP_VERIFY_ERR);
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
			LOG_DEBUG() << "passlabel was clicked";
		});

	connect(ui->pass_visible, &ClickedLabel::released, this, [this]()
		{
			ui->pass_edit->setEchoMode(QLineEdit::Password);
			LOG_DEBUG() << "passlabel was released";
		});

	connect(ui->pass2_visible, &ClickedLabel::clicked, this, [this]()
		{
			ui->pass2_edit->setEchoMode(QLineEdit::Normal);
			LOG_DEBUG() << "pass2label was clicked";
		});

	connect(ui->pass2_visible, &ClickedLabel::released, this, [this]()
		{
			ui->pass2_edit->setEchoMode(QLineEdit::Password);
			LOG_DEBUG() << "pass2label was released";
		});

	// 获取验证码按钮（get_token_button）不需要手动连接：
	// setupUi() 里的 QMetaObject::connectSlotsByName 会按 on_<对象名>_<信号> 规则
	// 自动把它连到 on_get_token_button_clicked()。
	// 确认按钮（confirm_button）同理自动连到 on_confirm_button_clicked()，
	// 绝不能再把 confirm_button 连到获取验证码的逻辑上。

	// 注册定时器（定时返回登录界面）
	m_timer = new QTimer(this);
	m_timer->stop();
	connect(m_timer, &QTimer::timeout, [this]()
		{
			if (m_countdown == 0)
			{
				m_timer->stop();
				emit sigSwitchLoginPage();
				return;
			}
			m_countdown--;
			ui->reg_success_tip_label->setText(
				QString("注册成功，%1 秒后返回登录").arg(m_countdown)
			);
		});

	// 返回按钮
	connect(ui->return_to_login, &QPushButton::clicked, this, [this]()
		{
			m_timer->stop();
			emit sigSwitchLoginPage();
			LOG_DEBUG() << "return to login page";
		});
	connect(ui->cancel_button, &QPushButton::clicked, this, [this]()
		{
			m_timer->stop();
			emit sigSwitchLoginPage();
			LOG_DEBUG() << "return to login page";
		});


	connect(&HttpManager::GetInstance(), &HttpManager::sig_reg_http_finish, this, &RegisterDialog::slot_reg_mod_finish);

	// 注册回调函数
	initHttpHandlers();
	ui->stackedWidget->setCurrentWidget(ui->page);
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
	if (!match)
	{
		showTip(tr("邮箱地址不准确"), false);
		return;
	}

	// 文档：POST /get_Verifycode 必填 email
	QJsonObject json_obj;
	json_obj.insert("email", email);

	showTip(tr("正在获取验证码..."), true);
	ui->get_token_button->setEnabled(false);

	HttpManager::GetInstance().postHttpReq(QUrl(GATE_URL_PREFIX + "/get_Verifycode"),
		json_obj,
		ReqId::ID_GET_VERIFY_CODE,
		Modules::REGISTER_MOD);
}

void RegisterDialog::slot_reg_mod_finish(ReqId id, QString res, ErrorCodes err)
{
	if (err != ErrorCodes::SUCCESS)
	{
		showTip(errorCodeToTip(err), false);
		ui->confirm_button->setEnabled(true);
		return;
	}

	// json解析
	QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
	if (jsonDoc.isNull())
	{
		showTip(tr("服务器返回数据解析失败"), false);
		ui->confirm_button->setEnabled(true);
		return;
	}
	if (!jsonDoc.isObject())
	{
		showTip(tr("服务器返回数据解析失败"), false);
		ui->confirm_button->setEnabled(true);
		return;
	}

	const auto iter = m_handlers.find(id);
	if (iter == m_handlers.end())
	{
		LOG_WARN() << "[register] no handler for req id:" << static_cast<int>(id);
		ui->confirm_button->setEnabled(true);
		return;
	}

	iter.value()(jsonDoc.object());
}

void RegisterDialog::initHttpHandlers()
{
	// 获取验证码回调函数
	m_handlers.insert(ReqId::ID_GET_VERIFY_CODE, [this](const QJsonObject& jsonObj)
		{
			const int error = jsonObj.value("error").toInt(ErrorCodes::ERROR_JSON);
			if (error != ErrorCodes::SUCCESS)
			{
				// 只有失败才立刻恢复按钮让用户重试；成功时按钮交给 TimerButton 的 10 秒冷却
				ui->get_token_button->setEnabled(true);

				// 文档：1001 参数缺失；1002 转调验证码服务失败
				showTip(errorCodeToTip(error), false);
				LOG_WARN() << "[register] get verify code failed, error:" << error << "json:" << jsonObj;
				return;
			}

			const QString email = jsonObj.value("email").toString();
			showTip(tr("验证码已发送到邮箱，注意查收"), true);
			LOG_INFO() << "[register] verify code sent to:" << email;

			// 文档：仅 Debug 构建（GATE_DEBUG）会返回 verify_code，用于本地联调
			if (jsonObj.contains("verify_code"))
			{
				LOG_DEBUG() << "[register] debug verify_code:" << jsonObj.value("verify_code").toString();
			}
		});

	// 注册用户回包逻辑
	m_handlers.insert(ReqId::ID_REG_USER, [this](const QJsonObject& jsonObj)
		{
			ui->confirm_button->setEnabled(true);

			const int error = jsonObj.value("error").toInt(ErrorCodes::ERROR_JSON);
			if (error != ErrorCodes::SUCCESS)
			{
				// 文档：1006 两次密码不一致、1003 验证码过期、1004 验证码错误、1010 注册失败
				showTip(errorCodeToTip(error), false);
				return;
			}

			// 文档：成功响应含 uid / email / user（昵称），不再回显 password
			LOG_INFO() << "[register] register success, uid:" << jsonObj.value("uid").toInt()
				<< "email:" << jsonObj.value("email").toString()
				<< "user:" << jsonObj.value("user").toString();

			showTip(tr("注册成功"), true);
			changeTipPage();
		});
}

void RegisterDialog::changeTipPage()
{
	m_timer->stop();
	m_countdown = 5;
	ui->stackedWidget->setCurrentWidget(ui->page_2);
	m_timer->start(1000);
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
	LOG_DEBUG() << "[ui] confirm_button_clicked";
	if (!checkEmailImputValid() ||
		!checkUserNameImputValid() ||
		!checkPasswordImputValid() ||
		!checkPassword2ImputValid() ||
		!checkVerifyImputValid())
	{
		return;
	}

	// 文档：/user_register 的 user 字段为昵称（字符串），并需同时提交 password / confirm / verify_code
	QJsonObject json_obj;
	json_obj.insert("user", ui->user_name_edit->text());
	json_obj.insert("email", ui->email_edit->text());
	json_obj.insert("password", ui->pass_edit->text());
	json_obj.insert("confirm", ui->pass2_edit->text());
	json_obj.insert("verify_code", ui->token_edit->text());

	showTip(tr("正在注册..."), true);
	ui->confirm_button->setEnabled(false);

	HttpManager::GetInstance().postHttpReq(QUrl(GATE_URL_PREFIX + "/user_register"),
		json_obj,
		ReqId::ID_REG_USER,
		Modules::REGISTER_MOD);
}
