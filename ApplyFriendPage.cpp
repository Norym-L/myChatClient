#include "ApplyFriendPage.h"

ApplyFriendPage::ApplyFriendPage(QWidget *parent)
    : QWidget(parent), ui(new Ui::ApplyFriendPageClass())
{
    ui->setupUi(this);
}

ApplyFriendPage::~ApplyFriendPage()
{
    delete ui;
}

