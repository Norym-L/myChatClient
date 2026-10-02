#include "adduseritem.h"
#include "ui_adduseritem.h"


AddUserItem::AddUserItem(QWidget* parent)
    : ListItemBase(parent), ui(new Ui::AddUserItem)
{
    ui->setupUi(this);
    setItemType(ListItemType::ADD_USER_TIP);
}

AddUserItem::~AddUserItem()
{
    delete ui;
}

QSize AddUserItem::sizeHint() const
{
    return {250, 70};
}
