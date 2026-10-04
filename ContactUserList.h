#pragma once
#include <QListWidget>

#include "global.h"

class ContactUserItem;

class ContactUserList : public QListWidget
{
	Q_OBJECT

public:
	explicit ContactUserList(QWidget* parent = nullptr);
	void showRedPoint(bool show = true);

protected:
	bool eventFilter(QObject* object, QEvent* event) override;

private:
	void addContactUserList();

private slots:
	void slotItemClicked(QListWidgetItem* item);
	// void slotAddAuthFriend(std::shared_ptr<AuthInfo>); // 对端同意添加好友
	// void slotAuthRsp(std::shared_ptr<AuthInfo>); // 自己同意添加好友

signals:
	void sigLoadingContactUser();
	void sigSwitchApplyFriendPage();
	void sigSwitchFriendInfoPage();

private:
	ContactUserItem* m_addFriendItem;
	QListWidgetItem* m_groupItem; // 分组的标签
};
