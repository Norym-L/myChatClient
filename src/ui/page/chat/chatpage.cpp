#include "ui/page/chat/chatpage.h"

#include "ui/components/ChatUserList.h"
#include <QWidget>
#include <QAction>

ChatPage::ChatPage(QWidget *parent)
    : QWidget(parent)
    , ui(new Ui::ChatPageClass())
{
    ui->setupUi(this);

#ifdef LOG_DEBUG // 仅在debug下显示接收按钮
    ui->receive_button->hide();
#endif

    ui->add_button->setState("normal", "hover", "press");
    initSearchEdit();
}

ChatPage::~ChatPage()
{
    delete ui;
}

void ChatPage::showSerachList(const bool is_searching)
{
    ui->chat_users_list->hide();
    ui->con_user_list->hide();
    ui->search_list->hide();

    if (is_searching)
    {
        ui->search_list->show();
        m_mode = ChatUiMode::SEARCH_MODE;
    }
    else if (m_state == ChatUiMode::CHAT_MODE)
    {
        ui->chat_users_list->show();
        m_mode = ChatUiMode::CHAT_MODE;
    }
    else if (m_state == ChatUiMode::CONTACT_MODE)
    {
        ui->con_user_list->show();
        m_mode = ChatUiMode::CONTACT_MODE;
    }
}

void ChatPage::initSearchEdit()
{
    /// 搜索图标
    ui->serach_edit->setMaxLen(100);
    auto *search_action = new QAction(ui->serach_edit);
    search_action->setIcon(QIcon(":/res/search.png"));
    ui->serach_edit->addAction(search_action, QLineEdit::LeadingPosition);
    ui->serach_edit->setPlaceholderText("搜索");

    /// 删除动作
    auto *clear_action = new QAction(ui->serach_edit);
    clear_action->setIcon(QIcon(":/res/close.png"));
    ui->serach_edit->addAction(clear_action, QLineEdit::TrailingPosition);
    connect(ui->serach_edit, &QLineEdit::textChanged, [this, clear_action](auto &text)
                {
                    if (text.isEmpty())
                    {
                        clear_action->setVisible(false);
                    }
                    else
                    {
                        clear_action->setVisible(true);
                        showSerachList(true);
                    }
                });
    // 删除文本
    connect(clear_action, &QAction::triggered, [this, clear_action]()
                {
                    ui->serach_edit->clear();
                    clear_action->setVisible(false);
                    ui->serach_edit->clearFocus();
                    showSerachList(false);
                });
    clear_action->setVisible(false);
    showSerachList(false);
}
