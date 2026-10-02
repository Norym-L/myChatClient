#pragma once

#include <QDialog>
#include "ui_ApplyFriend.h"

#include "clicked_label.h"
#include "FriendLabel.h"
#include "UserData.h"

QT_BEGIN_NAMESPACE
namespace Ui { class ApplyFriendClass; };
QT_END_NAMESPACE

class ApplyFriend : public QDialog
{
	Q_OBJECT

public:
	explicit ApplyFriend(QWidget *parent = nullptr);
	~ApplyFriend() override;

private:
	Ui::ApplyFriendClass *ui;
};

