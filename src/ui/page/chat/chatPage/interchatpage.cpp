#include "interchatpage.h"
#include "ui_interchatpage.h"


InterChatPage::InterChatPage(QWidget* parent)
    : QWidget(parent), ui(new Ui::InterChatPage)
{
    ui->setupUi(this);
#ifdef CLIENT_DEBUG
#else
    ui->receive_button->hide();
#endif

}

InterChatPage::~InterChatPage()
{
    delete ui;
}
