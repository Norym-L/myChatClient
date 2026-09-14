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

    connect(m_login_dialog, &LoginDialog::switchRegister, this, &MainWindow::slotSwitchReg);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::slotSwitchReg()
{
    m_register_dialog = new RegisterDialog(this);
    m_register_dialog->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);

    connect(m_register_dialog, &RegisterDialog::sigSwitchLoginPage, this, &MainWindow::slotSwitchLogin);
    setCentralWidget(m_register_dialog);
    m_login_dialog->hide();
    m_register_dialog->show();
}

void MainWindow::slotSwitchLogin()
{
    m_login_dialog = new LoginDialog(this);
    m_login_dialog->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
    setCentralWidget(m_login_dialog);

    m_register_dialog->hide();
    m_login_dialog->show();
    connect(m_login_dialog, &LoginDialog::switchRegister, this, &MainWindow::slotSwitchReg);
}
