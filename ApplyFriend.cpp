#include "ApplyFriend.h"

ApplyFriend::ApplyFriend(QWidget *parent)
	: QDialog(parent)
	, ui(new Ui::ApplyFriendClass())
{
	ui->setupUi(this);
}

ApplyFriend::~ApplyFriend()
{
	delete ui;
}

