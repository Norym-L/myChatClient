#ifndef CHATCLIENT_FINDRESULTDIALOG_H
#define CHATCLIENT_FINDRESULTDIALOG_H

#include <QDialog>
#include <memory>

#include "UserData.h"

QT_BEGIN_NAMESPACE

namespace Ui
{
	class FindResultDialog;
}

QT_END_NAMESPACE

class FindResultDialog : public QDialog
{
	Q_OBJECT

public:
	explicit FindResultDialog(QWidget* parent = nullptr);
	~FindResultDialog() override;

	/// 设置用户信息
	void setSearchInfo(std::shared_ptr<SearchInfo> si);

private:
	/// 刷新用户信息到显示
	void flashSearchInfo();

private slots:
	void slotOnAddFriendButtonClickedifFined();
	void slotOnAddFriendButtonClickednotFined();

private:
	Ui::FindResultDialog* ui;

	std::shared_ptr<SearchInfo> m_searchInfo;
	QWidget* m_parent;
};


#endif //CHATCLIENT_FINDRESULTDIALOG_H
