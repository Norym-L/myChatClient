#include "resetdialog.h"

#include <QJsonDocument>

#include "httpmanager.h"
#include "logger.h"

ResetDialog::ResetDialog(QWidget* parent)
	: QDialog(parent)
	, ui(new Ui::ResetDialogClass)
{
	ui->setupUi(this);
	ui->error_tip->clear();

	connect(ui->email_edit, &QLineEdit::editingFinished, this, [this]()
		{
			checkEmailInputValid();
		});
	connect(ui->new_password_edit, &QLineEdit::editingFinished, this, [this]()
		{
			checkPasswordInputValid();
		});
	connect(ui->verify_edit, &QLineEdit::editingFinished, this, [this]()
		{
			checkVerifyInputValid();
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
			if (!checkEmailInputValid())
			{
				return;
			}

			// 文档：POST /get_Verifycode 必填 email；本页的响应由 RESET_MOD 分发回来
			QJsonObject json_object;
			json_object.insert("email", ui->email_edit->text());

			showTip(tr("正在获取验证码..."), true);

			HttpManager::GetInstance().postHttpReq(
				QUrl(GATE_URL_PREFIX + "/get_Verifycode"),
				json_object,
				ReqId::ID_GET_VERIFY_CODE,
				Modules::RESET_MOD);
		});

	// 确认按钮
	connect(ui->ok_button, &QPushButton::clicked, this, [this]()
		{
			if (!checkEmailInputValid() ||
				!checkPasswordInputValid() ||
				!checkVerifyInputValid() ||
				!checkAccountInputValid())
			{
				return;
			}

			// 文档：/reset_password 的 user 字段为 uid，验证码字段为 verify_code。
			// 按《接口文档.md》约定 uid 以 uint64 的字符串形式提交；
			// 原先的 toInt() 会把超出 int 范围的输入静默截成 0，从而请求到 uid=0
			bool be_valid_uid = false;
			const std::uint64_t uid = ui->user_edit->text().toULongLong(&be_valid_uid);
			if (!be_valid_uid)
			{
				showTip(tr("账号超出有效范围"), false);
				return;
			}

			QJsonObject json_object;
			json_object.insert("user", QString::number(uid));
			json_object.insert("email", ui->email_edit->text());
			json_object.insert("password", ui->new_password_edit->text());
			json_object.insert("verify_code", ui->verify_edit->text());

			showTip(tr("正在重置密码..."), true);
			ui->ok_button->setEnabled(false);

			HttpManager::GetInstance().postHttpReq(
				QUrl(GATE_URL_PREFIX + "/reset_password"),
				json_object,
				ReqId::ID_RESET_PWD,
				Modules::RESET_MOD);
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

bool ResetDialog::checkAccountInputValid()
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

bool ResetDialog::checkEmailInputValid()
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

bool ResetDialog::checkPasswordInputValid()
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

bool ResetDialog::checkVerifyInputValid()
{
	auto verify = ui->verify_edit->text();
	if (verify.isEmpty())
	{
		addTipErr(TipErr::TIP_VERIFY_ERR, tr("验证码不能为空"));
		return false;
	}

	delTipErr(TipErr::TIP_VERIFY_ERR);
	return true;
}

void ResetDialog::showTip(const QString& str, const bool b_ok) const
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
	m_handlers.insert(ReqId::ID_GET_VERIFY_CODE, [this](const QJsonObject& json_object)
		{
			const int error = json_object.value("error").toInt(ErrorCodes::ERROR_JSON);
			if (error != ErrorCodes::SUCCESS)
			{
				// 文档：1001 参数缺失；1002 转调验证码服务失败
				showTip(errorCodeToTip(error), false);
				return;
			}

			showTip(tr("验证码已发送到邮箱，注意查收"), true);
			LOG_INFO() << "[reset] verify code sent to:" << json_object.value("email").toString();

			// 文档：仅 Debug 构建（GATE_DEBUG）会返回 verify_code，用于本地联调
			if (json_object.contains("verify_code"))
			{
				LOG_DEBUG() << "[reset] debug verify_code:" << json_object.value("verify_code").toString();
			}
		});

	m_handlers.insert(ReqId::ID_RESET_PWD, [this](const QJsonObject& json_object)
		{
			ui->ok_button->setEnabled(true);

			const int error = json_object.value("error").toInt(ErrorCodes::ERROR_JSON);
			if (error != ErrorCodes::SUCCESS)
			{
				// 文档：1003 验证码过期、1004 验证码错误、1013 后端故障、
				//       1007 uid 与邮箱不匹配、1008 密码更新失败
				showTip(errorCodeToTip(error), false);
				return;
			}

			// 文档：成功响应回显 email 与 verify_code，不再回显 passwd
			showTip(tr("重置成功，点击返回登录"), true);
			LOG_INFO() << "[reset] reset password success, email:" << json_object.value("email").toString()
				<< "verify_code:" << json_object.value("verify_code").toString();
		});
}

void ResetDialog::slot_resetModFinish(ReqId id, QString res, ErrorCodes error)
{
	if (error != ErrorCodes::SUCCESS)
	{
		showTip(errorCodeToTip(error), false);
		ui->ok_button->setEnabled(true);
		return;
	}

	QJsonDocument json_document = QJsonDocument::fromJson(res.toUtf8());
	if (json_document.isNull() ||
		!json_document.isObject())
	{
		showTip(tr("服务器返回数据解析失败"), false);
		ui->ok_button->setEnabled(true);
		return;
	}

	const auto iter = m_handlers.find(id);
	if (iter == m_handlers.end())
	{
		LOG_WARN() << "[reset] no handler for req id:" << static_cast<int>(id);
		ui->ok_button->setEnabled(true);
		return;
	}

	iter.value()(json_document.object());
}

ResetDialog::~ResetDialog()
{}
