#pragma once

#include <QDialog>

#include "app/global.h"
#include "ui_resetdialog.h"

class ResetDialog : public QDialog
{
	Q_OBJECT

public:
	ResetDialog(QWidget* parent = nullptr);
	~ResetDialog();

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

	/// 检查账号合法性
	bool checkAccountImputValid();
	/// 检查邮箱合法性
	bool checkEmailImputValid();
	/// 检查密码合法性
	bool checkPasswordImputValid();
	/// 检查验证码合法性
	bool checkVerifyImputValid();
	/// 显示错误提示
	void showTip(const QString& str, bool b_ok) const;
	/// 初始化处理函数
	void initHandlers();

signals:
	void sigSwitchLoginPage(void);

private slots:
	void slot_resetModFinish(ReqId id, QString res, ErrorCodes error);

private:
	Ui::ResetDialogClass* ui;
	QMap<ReqId, std::function<void(const QJsonObject&)>> m_handlers;
	QMap<TipErr, QString> m_tipErrs; // 错误码 - 错误提示
};

