#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "chatpage.h"
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
	/// 切换登录页（重置页返回时也走这里）
	void slotSwitchLogin();
	/// 切换重置页
	void slotSwitchReset();
	/// 切换聊天页面
	void slotSwitchChat();

private:
	/**
	 * @brief switchPage 切换中央部件，并销毁不再使用的旧页面
	 * @note 旧页面若不销毁，会继续响应 HttpManager / TCPManager 的信号，
	 *       造成同一次请求被多个页面重复处理（重复发起 TCP 连接等）
	 */
	void switchPage(QWidget* page);

	Ui::MainWindow* ui;
	LoginDialog* m_login_dialog = nullptr;
	RegisterDialog* m_register_dialog = nullptr;
	ResetDialog* m_reset_dialog = nullptr;
	ChatPage* m_chat_page = nullptr;
};
#endif // MAINWINDOW_H
