#include "FriendLabel.h"

FriendLabel::FriendLabel(QWidget *parent)
	: QFrame(parent), ui(new Ui::FriendLabelClass())
{
	ui->setupUi(this);
}

FriendLabel::~FriendLabel()
{
	delete ui;
}

