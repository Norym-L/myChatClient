#include "ApplyFriendItem.h"

ApplyFriendItem::ApplyFriendItem(QWidget* parent)
    : QWidget(parent), ui(new Ui::ApplyFriendItemClass())
{
    ui->setupUi(this);
}

ApplyFriendItem::~ApplyFriendItem()
{
    delete ui;
}
