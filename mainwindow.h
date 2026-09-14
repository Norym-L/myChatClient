#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "logindialog.h"
#include "registerdialog.h"
#include "resetdialog.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
	class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit MainWindow(QWidget* parent = nullptr);
	~MainWindow() override;

public slots:
	/// 切换注册页
	void slotSwitchReg();
	/// 切换登录页
	void slotSwitchLogin();
	/// 重置页返回登录页
	void slotSwitchLogin2();
	/// 切换重置页
	void slotSwitchReset();
private:
	Ui::MainWindow* ui;
	LoginDialog* m_login_dialog;
	RegisterDialog* m_register_dialog;
	ResetDialog* m_reset_dialog;
};
#endif // MAINWINDOW_H
