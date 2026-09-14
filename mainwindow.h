#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "logindialog.h"
#include "registerdialog.h"

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

public slots:
    /// 切换注册页
    void slotSwitchReg();
    /// 切换登录页
    void slotSwitchLogin();

private:
    Ui::MainWindow *ui;
    LoginDialog *m_login_dialog;
    RegisterDialog *m_register_dialog;

};
#endif // MAINWINDOW_H
