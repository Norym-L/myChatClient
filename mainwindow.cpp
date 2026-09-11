#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    m_login_dialog = new LoginDialog(this);
    m_login_dialog->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(m_login_dialog);

    //创建和注册消息的链接
    connect(m_login_dialog, &LoginDialog::switchRegister, this, &MainWindow::SlotSwitchReg);
    m_register_dialog = new RegisterDialog(this);
    m_register_dialog->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::SlotSwitchReg()
{
    setCentralWidget(m_register_dialog);
    m_login_dialog->hide();
    m_register_dialog->show();
}
