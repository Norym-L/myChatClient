#include "chatpage.h"

ChatPage::ChatPage(QWidget *parent)
	: QWidget(parent)
	, ui(new Ui::ChatPageClass())
{
	ui->setupUi(this);
}

ChatPage::~ChatPage()
{
	delete ui;
}

