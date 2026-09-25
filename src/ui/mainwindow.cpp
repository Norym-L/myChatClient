#include "ui/mainwindow.h"
#include "ui_mainwindow.h"

#include "core/logger.h"
#include "network/TCPManager.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    slotSwitchLogin();

#ifdef LOG_DEBUG //TODO debug直接跳转chat页面做测试
    slotSwitchChat();
#endif
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::switchPage(QWidget* page)
{
    QWidget* old_page = centralWidget();
    if (old_page == page)
    {
        page->show();
        return;
    }

    setCentralWidget(page);
    page->show();
    LOG_DEBUG() << "[ui] switched page to" << page->metaObject()->className();

    if (old_page == nullptr)
    {
        return;
    }

    // 旧页面不再使用：先清空成员，再销毁，避免野指针
    if (old_page == m_login_dialog)
    {
        m_login_dialog = nullptr;
    }
    if (old_page == m_register_dialog)
    {
        m_register_dialog = nullptr;
    }
    if (old_page == m_reset_dialog)
    {
        m_reset_dialog = nullptr;
    }
    if (old_page == m_chat_page)
    {
        m_chat_page = nullptr;
    }

    old_page->hide();
    old_page->deleteLater();
}

void MainWindow::slotSwitchReg()
{
    if (m_register_dialog == nullptr)
    {
        auto* register_dialog = new RegisterDialog(this);
        register_dialog->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
        connect(register_dialog, &RegisterDialog::sigSwitchLoginPage, this, &MainWindow::slotSwitchLogin);
        m_register_dialog = register_dialog;
    }

    switchPage(m_register_dialog);
}

void MainWindow::slotSwitchLogin()
{
    this->setMinimumSize(QSize(350, 500));
    this->setMaximumSize(QSize(350, 500));
    this->resize(QSize(350, 500));

    if (m_login_dialog == nullptr)
    {
        auto* login_dialog = new LoginDialog(this);
        login_dialog->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
        // 连接登录界面注册信号
        connect(login_dialog, &LoginDialog::switchRegister, this, &MainWindow::slotSwitchReg);
        // 连接登录界面忘记密码信号
        connect(login_dialog, &LoginDialog::switchReset, this, &MainWindow::slotSwitchReset);
        // 连接登录成功进入聊天页面信号
        connect(login_dialog, &LoginDialog::sig_switchChatPage, this, &MainWindow::slotSwitchChat);
        m_login_dialog = login_dialog;
    }

    switchPage(m_login_dialog);
}

void MainWindow::slotSwitchReset()
{
    if (m_reset_dialog == nullptr)
    {
        auto* reset_dialog = new ResetDialog(this);
        reset_dialog->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
        connect(reset_dialog, &ResetDialog::sigSwitchLoginPage, this, &MainWindow::slotSwitchLogin);
        m_reset_dialog = reset_dialog;
    }

    switchPage(m_reset_dialog);
}

void MainWindow::slotSwitchChat()
{
    if (m_chat_page == nullptr)
    {
        auto* chat_page = new ChatPage(this);
        chat_page->setWindowFlags(Qt::CustomizeWindowHint | Qt::FramelessWindowHint);
        // 连接聊天页面返回登录页面信号
        connect(chat_page, &ChatPage::sigSwitchLoginPage, this, &MainWindow::slotSwitchLogin);
        m_chat_page = chat_page;
    }

    this->setMinimumSize(QSize(650, 500));
    this->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    this->resize(QSize(700, 600));
    switchPage(m_chat_page);
}
