#include "ui/page/chat/chatpage.h"

#include "ui/components/ChatUserList.h"
#include "core/logger.h"
#include <QWidget>
#include <QAction>

#include "friendWidget/friendwidget.h"

ChatPage::ChatPage(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::ChatPageClass())
{
    ui->setupUi(this);

#ifdef CLIENT_DEBUG // 仅在debug下显示接收按钮
    test_addChatUserList();
#else
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
    ui->search_edit->setMaxLen(100);
    auto* search_action = new QAction(ui->search_edit);
    search_action->setIcon(QIcon(":/res/search.png"));
    ui->search_edit->addAction(search_action, QLineEdit::LeadingPosition);
    ui->search_edit->setPlaceholderText("搜索");

    /// 删除动作
    auto* clear_action = new QAction(ui->search_edit);
    clear_action->setIcon(QIcon(":/res/close.png"));
    ui->search_edit->addAction(clear_action, QLineEdit::TrailingPosition);
    connect(ui->search_edit, &QLineEdit::textChanged, [this, clear_action](auto& text)
                {
                    if (text.isEmpty())
                    {
                        clear_action->setVisible(false);
                        showSerachList(false);
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
                    ui->search_edit->clear();
                    clear_action->setVisible(false);
                    ui->search_edit->clearFocus();
                    showSerachList(false);
                });
    clear_action->setVisible(false);
    showSerachList(false);
}



#ifdef CLIENT_DEBUG
#include <QRandomGenerator>
void ChatPage::test_addChatUserList() const
{
    std::vector<QString> strs = {
        "hello world !",
        "nice to meet u",
        "New year，new life",
        "You have to love yourself",
        "My love is written in the wind ever since the whole world is you"
    };
    std::vector<QString> heads = {
        ":/assets/images/head_1.jpg",
        ":/assets/images/head_2.jpg",
        ":/assets/images/head_3.jpg",
        ":/assets/images/head_4.jpg",
        ":/assets/images/head_5.jpg"
    };
    std::vector<QString> names = {
        "llfc",
        "zack",
        "golang",
        "cpp",
        "java",
        "nodejs",
        "python",
        "rust"
    };

    for (int i = 0; i < 13; ++i)
    {
        const int random_value = QRandomGenerator::global()->bounded(100);
        auto str_i = random_value % strs.size();
        auto head_i = random_value % heads.size();
        auto name_i = random_value % names.size();

        auto* friend_widget = new FriendWidget();
        friend_widget->setInfo(names[name_i], heads[head_i], strs[str_i]);
        auto* item = new QListWidgetItem;
        item->setSizeHint(friend_widget->sizeHint());
        ui->chat_users_list->addItem(item);
        ui->chat_users_list->setItemWidget(item, friend_widget);
    }
}
#endif
