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
	void setSearchInfo(const std::shared_ptr<SearchInfo>& si);

private slots:
	void slotOnAddFriendButtonClicked();

private:
	Ui::FindResultDialog* ui;

	std::shared_ptr<SearchInfo> m_searchInfo;
	QWidget* m_parent;
};


#endif //CHATCLIENT_FINDRESULTDIALOG_H
