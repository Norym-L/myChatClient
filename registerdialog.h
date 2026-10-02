#ifndef REGISTERDIALOG_H
#define REGISTERDIALOG_H

#include <QTimer>
#include <QDialog>

#include "global.h"

namespace Ui
{
	class RegisterDialog;
}

class RegisterDialog : public QDialog
{
	Q_OBJECT

public:
	explicit RegisterDialog(QWidget* parent = nullptr);
	~RegisterDialog();

private:
	/**
	 * 添加错误提示
	 * @param tip 错误码
	 * @param notice 错误提示
	 */
	void addTipErr(const TipErr tip, const QString& notice);

	/**
	 * 删除错误提示
	 * @param tip 错误码
	 */
	void delTipErr(const TipErr tip);

	/// 检查用户名合法性
	bool checkUserNameInputValid();
	/// 检查邮箱合法性
	bool checkEmailInputValid();
	/// 检查密码合法性
	bool checkPasswordInputValid();
	/// 检查再次输入密码合法性
	bool checkPassword2InputValid();
	/// 检查验证码合法性
	bool checkVerifyInputValid();

private slots:
	void on_get_token_button_clicked();
	void slot_reg_mod_finish(ReqId id, QString res, ErrorCodes err);

	void on_confirm_button_clicked();

signals:
	/// 返回登录页面
	void sigSwitchLoginPage(void);

private:
	QTimer* m_timer = nullptr;
	int m_countdown = 5; // 注册成功提示页面停留时间
	void initHttpHandlers();
	void changeTipPage();
	void showTip(const QString& str, bool b_ok);
	Ui::RegisterDialog* ui;
	QMap<ReqId, std::function<void(const QJsonObject&)>> m_handlers;
	QMap<TipErr, QString> m_tipErrs; // 错误码 - 错误提示
};

#endif // REGISTERDIALOG_H
