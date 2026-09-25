#include "ui/page/chat/chatpage.h"

ChatPage::ChatPage(QWidget *parent)
	: QWidget(parent)
	, ui(new Ui::ChatPageClass())
{
	ui->setupUi(this);
#ifndef _DEBUG // 仅在debug下显示接收按钮
	ui->receive_button->hide();
#endif

	ui->add_button->setState("normal", "hover", "press");
}

ChatPage::~ChatPage()
{
	delete ui;
}

