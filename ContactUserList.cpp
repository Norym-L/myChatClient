#include "ContactUserList.h"

#include "ContactUserItem.h"
#include "GroupTipItem.h"
#include "TCPManager.h"

#include <QWheelEvent>
#include <QScrollBar>

#include "logger.h"

#ifdef _DEBUG // TODO 测试添加联系人
#include <QRandomGenerator>
#endif

ContactUserList::ContactUserList(QWidget* parent)
    : QListWidget(parent)
{
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    viewport()->installEventFilter(this);

    addContactUserList();
    connect(this, &QListWidget::itemClicked, this, &ContactUserList::slotItemClicked);
    // connect(&TCPManager::GetInstance(), &TCPManager::sigAddAuthFriend, this, &ContactUserList::slotAddAuthFriend);
    // connect(&TCPManager::GetInstance(), &TCPManager::sigAuthRsp, this, &ContactUserList::slotAuthRsp);
}

void ContactUserList::showRedPoint(const bool show) const
{
    m_addFriendItem->showRedPoint(show);
}

bool ContactUserList::eventFilter(QObject* object, QEvent* event)
{
    // 检查事件是否是鼠标悬浮进入或离开
    if (object == this->viewport())
    {
        if (event->type() == QEvent::Enter)
        {
            // 鼠标悬浮，显示滚动条
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }
        else if (event->type() == QEvent::Leave)
        {
            // 鼠标离开，隐藏滚动条
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }

    // 检查事件是否是鼠标滚轮事件
    if (object == this->viewport() && event->type() == QEvent::Wheel)
    {
        const QWheelEvent* wheelEvent = dynamic_cast<QWheelEvent*>(event);
        const int numDegrees = wheelEvent->angleDelta().y() / 8;
        const int numSteps = numDegrees / 15; // 计算滚动步数

        // 设置滚动幅度
        this->verticalScrollBar()->setValue(this->verticalScrollBar()->value() - numSteps);

        // 检查是否滚动到底部
        const QScrollBar* scrollBar = this->verticalScrollBar();
        const int maxScrollValue = scrollBar->maximum();
        const int currentValue = scrollBar->value();
        //int pageSize = 10; // 每页加载的联系人数量

        if (maxScrollValue - currentValue <= 0)
        {
            // 滚动到底部，加载新的联系人
            LOG_DEBUG() << "load more contact user";
            //发送信号通知聊天界面加载更多聊天内容
            emit sigLoadMoreContactUsers();
        }

        return true; // 停止事件传递
    }

    return QListWidget::eventFilter(object, event);
}

void ContactUserList::addContactUserList()
{
    auto* group_tip = new GroupTipItem(tr("新的朋友"));
    auto* item = new QListWidgetItem();
    item->setSizeHint(group_tip->sizeHint());
    addItem(item);
    setItemWidget(item, group_tip);
    item->setFlags(item->flags() & ~Qt::ItemIsSelectable);

    m_addFriendItem = new ContactUserItem();
    m_addFriendItem->setObjectName("newFriendItem");
    m_addFriendItem->setInfo(0, tr("新的朋友"), ":/assets/images/addUser.png");
    m_addFriendItem->setItemType(ListItemType::APPLY_FRIEND);
    auto* add_item = new QListWidgetItem();
    add_item->setSizeHint(m_addFriendItem->sizeHint());
    addItem(add_item);
    setItemWidget(add_item, m_addFriendItem); // 替换 add_item 为自定义的 m_addFriendItem
    setCurrentItem(add_item); // 设置默认选中条目

    auto* group_contact = new GroupTipItem(tr("联系人"));
    m_groupItem = new QListWidgetItem();
    m_groupItem->setSizeHint(group_contact->sizeHint());
    addItem(m_groupItem);
    setItemWidget(m_groupItem, group_contact);
    m_groupItem->setFlags(m_groupItem->flags() & ~Qt::ItemIsSelectable);

#ifdef _DEBUG // TODO: 模拟增加用户

    static std::vector<QString> strs = {
        "hello world !",
        "nice to meet u",
        "New year，new life",
        "You have to love yourself",
        "My love is written in the wind ever since the whole world is you"
    };
    static std::vector<QString> heads = {
        ":/assets/images/head_1.jpg",
        ":/assets/images/head_2.jpg",
        ":/assets/images/head_3.jpg",
        ":/assets/images/head_4.jpg",
        ":/assets/images/head_5.jpg"
    };
    static std::vector<QString> names = {
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
        const auto str_i = random_value % strs.size();
        const auto head_i = random_value % heads.size();
        const auto name_i = random_value % names.size();

        auto* contact_user_widget = new ContactUserItem();
        contact_user_widget->setInfo(0, names[name_i], heads[head_i], strs[str_i]);
        auto* item = new QListWidgetItem;
        item->setSizeHint(contact_user_widget->sizeHint());
        addItem(item);
        setItemWidget(item, contact_user_widget);
    }
#endif
}

void ContactUserList::slotItemClicked(QListWidgetItem* item)
{
    auto* widget = itemWidget(item);
    if (!widget)
    {
        return;
    }

    const auto* custom_tiem = qobject_cast<ListItemBase*>(widget);
    if (!custom_tiem)
    {
        return;
    }

    switch (custom_tiem->getType())
    {
    case ListItemType::GROUP_TIP:
    case ListItemType::INVALID:
        {
        LOG_DEBUG() << "invalid | group tip item clicked";
        break;
        }
    case ListItemType::APPLY_FRIEND:
        {
        // 创建对话框 提示用户
        LOG_DEBUG() << "apply friend item clicked";
        emit sigSwitchApplyFriendPage();
        break;
        }
    case ListItemType::CONTACT_USER:
        {
        // 跳转到联系人信息界面
        LOG_DEBUG() << "contact user item clicked";
        emit sigSwitchFriendInfoPage();
        break;
        }
    default:
        {
        LOG_WARN() << "[chat page][contact list] a invalid type item clicked type: " << static_cast<int>(custom_tiem->getType());
        }
    }
}
