#include "SearchList.h"

#include <QEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include "TCPManager.h"
#include "adduseritem.h"
#include "findresultdialog.h"
#include "logger.h"
#include "TruncateEdit.h"


SearchList::SearchList(QWidget* parent)
	: QListWidget(parent), m_findDialog(new FindResultDialog(this))
{
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    // 安装事件过滤器
    this->viewport()->installEventFilter(this);
    // item点击事件
    connect(this, &QListWidget::itemClicked, this, &SearchList::slotItemClicked);
    addTipItem();
    // 搜索好友
    connect(&TCPManager::GetInstance(), &TCPManager::sigUserSearchFinish, this, &SearchList::slotUserSearchFinish);
}

void SearchList::closeFindDialog()
{
    if (m_findDialog)
    {
        m_findDialog->hide();
    }
}

void SearchList::waitPending(const bool pending)
{
    m_sendPending = pending;
    if (pending)
    {
        m_loadingDialog = new LoadingDialog(this);
        m_loadingDialog->setModal(true);
        m_loadingDialog->show();
    }
    else
    {
        m_loadingDialog->hide();
        m_loadingDialog->deleteLater();
    }
}

void SearchList::addTipItem()
{
    auto* invalid_item = new QWidget();
    auto* item_tmp = new QListWidgetItem();
    item_tmp->setSizeHint({250, 10});
    this->addItem(item_tmp);
    invalid_item->setObjectName("invalid_item");
    this->setItemWidget(item_tmp, invalid_item);
    item_tmp->setFlags(item_tmp->flags() & ~Qt::ItemIsSelectable);

    auto* add_user_item = new AddUserItem();
    auto* item = new QListWidgetItem();
    item->setSizeHint(add_user_item->sizeHint());
    this->addItem(item);
    this->setItemWidget(item, add_user_item);
}

bool SearchList::eventFilter(QObject* object, QEvent* event)
{
    if (object == this->viewport())
    {
        if (event->type() == QEvent::Enter) // 鼠标进入悬浮 显示滚动条
        {
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }
        else if (event->type() == QEvent::Leave)
        {
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }
    if (object == this->viewport() && event->type() == QEvent::Wheel) // 滚轮事件
    {
        const QWheelEvent* wheelEvent = dynamic_cast<QWheelEvent*>(event);
        const int num_degrees = wheelEvent->angleDelta().y() / 8;
        const int num_steps = num_degrees / 15;
        this->verticalScrollBar()->setValue(this->verticalScrollBar()->value() - num_steps);
        return true;
    }

    return QListWidget::eventFilter(object, event);
}

void SearchList::slotItemClicked(QListWidgetItem* item)
{
    auto* widget = itemWidget(item);
    if (!widget)
    {
        LOG_DEBUG() << "[chat page][search list] slot item clicked widget is nullptr";
        return;
    }

    const auto* custom_item = dynamic_cast<ListItemBase*>(widget);
    if (!custom_item)
    {
        LOG_DEBUG() << "[chat page][search list] slot item clicked widget is nullptr";
        return;
    }

    const auto item_type = custom_item->getType();
    if (item_type == ListItemType::INVALID)
    {
        return;
    }
    else if (item_type == ListItemType::ADD_USER_TIP)
    {
        if (m_sendPending)
        {
            return;
        }
        waitPending(true);

        // 发送请求给服务器
        // 注意：搜索字段按《接口文档.md》是「对方的 uid 或用户名」，此处原样透传用户输入
        // （输入 uid 时本身就是十进制字符串）。不要走 parseUid —— 那样就无法按用户名搜索
        QJsonObject json_object;
        json_object["uid"] = m_searchEditText;

        const QJsonDocument document(json_object);
        QByteArray json_data = document.toJson(QJsonDocument::Compact);
        // 发送tcp请求
        emit TCPManager::GetInstance().sigSendData(ReqId::ID_SEARCH_USER_REQ, std::move(json_data));
        LOG_DEBUG() << "[search] send search req, text: " << m_searchEditText;
        return;
    }

    closeFindDialog();
}

void SearchList::slotUserSearchFinish(std::shared_ptr<SearchInfo> info)
{
    waitPending(false);
    if (m_findDialog == nullptr)
    {
        m_findDialog = new FindResultDialog(this);
    }
    // TODO 判断是否已经是好友
    m_findDialog->setSearchInfo(std::move(info)); // 未找到时传入 nullptr 在里面处理
    m_findDialog->show();
}

void SearchList::slotSyncSearchEditChanged(const QString& text) // 在 chatpage 的构造函数中连接
{
    m_searchEditText = text;
}
