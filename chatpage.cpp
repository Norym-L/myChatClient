#include "chatpage.h"

#include "ChatUserList.h"
#include "logger.h"
#include <QWidget>
#include <QAction>
#include <QAbstractScrollArea>
#include <QMouseEvent>

#include "friendwidget.h"
#include "loadingdialog.h"

namespace
{
/// 滚动条宽度，需与 assets/styles/common.qss 中 QScrollBar 的 width / height 保持一致
constexpr int kScrollBarWidth = 8;

/**
 * @brief 访问器：把 QAbstractScrollArea 的 protected setViewportMargins 提升为可调用
 *
 * Qt 6 中 setViewportMargins 是 protected，外部代码无法直接调用，也不可
 * 把基类指针强转成派生类再调用（对象实际不是派生类型，属未定义行为）。
 * 这里在派生类内部取该成员的成员函数指针 —— 派生类内部访问基类 protected
 * 成员是标准允许的，且指针类型仍是
 * void (QAbstractScrollArea::*)(int, int, int, int)，可合法作用于任意滚动区域。
 *
 * ⚠ 别写成 &ScrollAreaAccess::setViewportMargins：用派生类名限定后，
 *   MSVC 认为该指针的类型是 void (ScrollAreaAccess::*)(...)，而成员函数指针
 *   只允许 基类→派生类 的转换、反过来不存在，于是报 C2440
 *   （GCC 会把它判成基类指针类型而放行，这就是同一段代码两套编译器表现不同的原因）。
 *   直接用基类名 QAbstractScrollArea:: 限定，类型天然就是基类的，两套编译器一致。
 */
class ScrollAreaAccess : public QAbstractScrollArea
{
public:
    static void setViewportMargins(QAbstractScrollArea* area,
                                   const int left, const int top,
                                   const int right, const int bottom)
    {
        using SetViewportMarginsFn = void (QAbstractScrollArea::*)(int, int, int, int);
        // 该名字有 (int,int,int,int) 与 (QMargins) 两个重载，由目标类型选定前者
        const SetViewportMarginsFn setter = &QAbstractScrollArea::setViewportMargins;
        (area->*setter)(left, top, right, bottom);
    }
};

/**
 * @brief 让纵向滚动条悬浮在内容之上
 * @param area 目标滚动区域；负值右边距使 viewport 延伸到滚动条区域下方，
 *             配合 QSS 中透明的滚动条轨道，滚动条不再占用内容宽度
 */
void overlayScrollBar(QAbstractScrollArea* area)
{
    ScrollAreaAccess::setViewportMargins(area, 0, 0, -kScrollBarWidth, 0);
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
    initSideBar();

    installEventFilter(this);
}

ChatPage::~ChatPage()
{
    delete ui;
}

bool ChatPage::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress)
    {
        auto* mouse_event = dynamic_cast<QMouseEvent*>(event);
        handleGrobalMousePress(mouse_event);
    }

    return QWidget::eventFilter(watched, event);
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

    /// 删除动作("X"图标)
    // 显示隐藏搜索列表放到 slotSideBarSwitchToContact() 中处理
    auto* clear_action = new QAction(ui->search_edit);
    clear_action->setIcon(QIcon(":/res/close.png"));
    ui->search_edit->addAction(clear_action, QLineEdit::TrailingPosition);
    // 有文本变化时控制清除按钮的显示
    connect(ui->search_edit, &QLineEdit::textChanged, [clear_action](auto& text)
                {
                    if (text.isEmpty()) // 设置清除按钮不可见
                    {
                        clear_action->setVisible(false);
                    }
                    else
                    {
                        clear_action->setVisible(true);
                    }
                });
    // 点击删除按钮时删除文本
    connect(clear_action, &QAction::triggered, [this]()
                {
                    ui->search_edit->clear();
                    // clear_action->setVisible(false);
                    ui->search_edit->clearFocus();
                    // showSerachList(false);
                });
    clear_action->setVisible(false);
    showSerachList(false);

    // 连接输入框发生变化
    connect(ui->search_edit, &QLineEdit::textChanged, this, &ChatPage::slotSearchEditTextChanged);
}

void ChatPage::initChatUserList()
{
    // 收到信号加载更多联系人
    connect(ui->chat_users_list, &ChatUserList::sigLoadingChatUser, this, &ChatPage::slotLoadingMoreChatUserInChatUserList);
}

void ChatPage::initSideBar()
{
    QPixmap pixmap(":/assets/images/head_4.jpg");
    pixmap = pixmap.scaled(ui->side_head_label->size(), Qt::KeepAspectRatio);
    ui->side_head_label->setPixmap(pixmap);
    ui->side_head_label->setScaledContents(true); // 设置头像

    ui->side_chat_widget->setState();
    ui->side_chat_widget->setCurrentState(ClickLabelState::SELECT);
    ui->side_contact_widget->setState();
    ui->side_contact_widget->setCurrentState(ClickLabelState::NORMAL);

    addButtonToGroup(ui->side_chat_widget, "sideBar");
    addButtonToGroup(ui->side_contact_widget, "sideBar");

    connect(ui->side_chat_widget, &StateWidget::released, this, &ChatPage::slotSideBarSwitchToChat);
    connect(ui->side_contact_widget, &StateWidget::released, this, &ChatPage::slotSideBarSwitchToContact);
}

void ChatPage::addButtonToGroup(StateWidget* widget, const QString& group_name)
{
    if (m_groupedWidgets.contains(group_name))
    {
        m_groupedWidgets[group_name].append(widget);
    }
    else
    {
        m_groupedWidgets[group_name] = {widget};
    }
}

void ChatPage::freshSideBarStateToSelected(StateWidget* widget, const QString& group_name)
{
    for (auto* item: m_groupedWidgets[group_name])
    {
        item->setCurrentState(ClickLabelState::NORMAL);
    }
    widget->setCurrentState(ClickLabelState::SELECT);
}

void ChatPage::handleGrobalMousePress(const QMouseEvent* event)
{
    // 搜索模式，点击空白处隐藏搜索列表
    if (m_mode != ChatUiMode::SEARCH_MODE)
    {
        return;
    }
    const QPoint pos_in_serach_list = ui->search_list->mapFromGlobal(event->globalPosition().toPoint());
    if (!ui->search_list->rect().contains(pos_in_serach_list))
    {
        ui->search_edit->clear();
        showSerachList(false);
    }
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
    auto* loading_dialog = new LoadingDialog(this); // TODO: loading_dialog 改到list中,并且实现异步加载
    loading_dialog->setModal(true);
    loading_dialog->show();
    LOG_DEBUG() << "[ui][chat page] loading more user function run";
    test_addChatUserList();
    loading_dialog->deleteLater();
    m_beLoadingMoreChatUser = false;
}

void ChatPage::slotSideBarSwitchToChat()
{
    LOG_DEBUG() << "[ui][chat page] slotSideBarSwitchToChat";
    ui->stackedWidget->setCurrentWidget(ui->chat_page);
    freshSideBarStateToSelected(ui->side_chat_widget, "sideBar");
    m_state = ChatUiMode::CHAT_MODE;
    showSerachList(false);
}

void ChatPage::slotSideBarSwitchToContact()
{
    LOG_DEBUG() << "[ui][chat page] slotSideBarSwitchToContact";
    ui->stackedWidget->setCurrentWidget(ui->friend_apply_page);
    freshSideBarStateToSelected(ui->side_contact_widget, "sideBar");
    m_state = ChatUiMode::CONTACT_MODE;
    showSerachList(false);
}

void ChatPage::slotSearchEditTextChanged(const QString& text)
{
    showSerachList(!text.isEmpty()); // 设置搜索框是否显示

}
