#include "ContactUserList.h"

#include "TCPManager.h"

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

void ContactUserList::showRedPoint(bool show)
{}

bool ContactUserList::eventFilter(QObject* object, QEvent* event)
{
	return QListWidget::eventFilter(object, event);
}

void ContactUserList::addContactUserList()
{}

void ContactUserList::slotItemClicked(QListWidgetItem* item)
{}
