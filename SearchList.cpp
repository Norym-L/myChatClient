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
	connect(&TCPManager::GetInstance(), &TCPManager::sigUserSearch, this, &SearchList::slotUserSearch);
}

void SearchList::closeFindDialog()
{
	if (m_findDialog)
	{
		m_findDialog->hide();
	}
}

void SearchList::setSearchedit(QWidget* widget)
{
}

void SearchList::waitPending(bool pending)
{
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
		// test
		const auto search_info = std::make_shared<SearchInfo>(0, "csm", "曹世明", "from wzu", Sex::FEMALE);

		LOG_DEBUG() << "[chat page][search list] show addFriend dailog";
		m_findDialog->setSearchInfo(search_info);
		m_findDialog->show();
		// if (m_sendPending)
		// {
		//  return;
		// }
		//    waitPending(true);
		//    const auto search_edit = dynamic_cast<TruncateEdit*>(m_searchEdit);
		//    const auto uid_str = search_edit->text();
		//
		//    // 发送请求给服务器
		//    QJsonObject json_object;
		//    json_object["uid"] = uid_str;
		//
		//    const QJsonDocument document(json_object);
		//    const QString json_str = document.toJson(QJsonDocument::Indented);
		//    // 发送tcp请求
		//    emit TCPManager::GetInstance().sigSendData(ReqId::ID_SEARCH_USER_REQ, json_str);
		return;
	}

	closeFindDialog();
}

void SearchList::slotUserSearch(std::shared_ptr<SearchInfo> info)
{
}
