#pragma once

#include <QWidget>
#include "ui_chatpage.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
	class ChatPageClass;
};

QT_END_NAMESPACE

class ChatPage : public QWidget
{
	Q_OBJECT

public:
	ChatPage(QWidget* parent = nullptr);
	~ChatPage();

signals:
	void sigSwitchLoginPage(void);

private:
	Ui::ChatPageClass* ui;
};
