#include "ui/page/chat/chatpage.h"

#include "ui/components/ChatUserList.h"
#include "core/logger.h"
#include <QWidget>
#include <QAction>
#include <QAbstractScrollArea>

#include "friendWidget/friendwidget.h"
#include "loadingDialog/loadingdialog.h"

namespace
{
/// 滚动条宽度，需与 assets/styles/common.qss 中 QScrollBar 的 width / height 保持一致
constexpr int kScrollBarWidth = 8;

/**
 * @brief 访问器：把 QAbstractScrollArea 的 protected setViewportMargins 提升为 public
 *
 * Qt 6 中 setViewportMargins 是 protected，外部代码无法直接调用，也不可
 * 把基类指针强转成派生类再调用（对象实际不是派生类型，属未定义行为）。
 * 这里通过 using 声明提升访问权限后取成员函数指针，所得指针类型仍是
 * void (QAbstractScrollArea::*)(int, int, int, int)，可合法作用于任意滚动区域。
 */
class ScrollAreaAccess : public QAbstractScrollArea
{
public:
    using QAbstractScrollArea::setViewportMargins;
};

/**
 * @brief 让纵向滚动条悬浮在内容之上
 * @param area 目标滚动区域；负值右边距使 viewport 延伸到滚动条区域下方，
 *             配合 QSS 中透明的滚动条轨道，滚动条不再占用内容宽度
 */
void overlayScrollBar(QAbstractScrollArea* area)
{
    using SetViewportMarginsFn = void (QAbstractScrollArea::*)(int, int, int, int);
    const SetViewportMarginsFn setter = &ScrollAreaAccess::setViewportMargins;
    (area->*setter)(0, 0, -kScrollBarWidth, 0);
}
}

ChatPage::ChatPage(QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::ChatPageClass())
{
    ui->setupUi(this);

#ifdef CLIENT_DEBUG // 仅在debug下显示接收按钮
    test_addChatUserList();
#else

#endif

    // 会话列表 / 搜索列表 / 联系人列表：滚动条统一改为悬浮
    overlayScrollBar(ui->chat_users_list);
    overlayScrollBar(ui->search_list);
    overlayScrollBar(ui->con_user_list);

    ui->add_button->setState("normal", "hover", "press");
    initSearchEdit();
    initChatUserList();
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

void ChatPage::initChatUserList()
{
    // 收到信号加载更多联系人
    connect(ui->chat_users_list, &ChatUserList::sigLoadingChatUser, this, &ChatPage::slotLoadingMoreChatUserInChatUserList);
}


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

void ChatPage::slotLoadingMoreChatUserInChatUserList()
{
    if (m_beLoadingMoreChatUser)
    {
        return;
    }
    m_beLoadingMoreChatUser = true;
    auto* loading_dialog = new LoadingDialog(this);
    loading_dialog->setModal(true);
    loading_dialog->show();
    LOG_DEBUG() << "[ui][chat page] loading more user function run";
    test_addChatUserList();
    loading_dialog->deleteLater();
    m_beLoadingMoreChatUser = false;
}
