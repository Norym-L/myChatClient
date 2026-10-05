#include "GroupTipItem.h"

GroupTipItem::GroupTipItem(QWidget* parent)
    : ListItemBase(parent), ui(new Ui::GroupTipItemClass())
{
    ui->setupUi(this);
    setItemType(ListItemType::GROUP_TIP);
}

GroupTipItem::GroupTipItem(const QString& title, QWidget* parent)
    : ListItemBase(parent), ui(new Ui::GroupTipItemClass()), m_groupTitle(title)
{
    ui->setupUi(this);
    setItemType(ListItemType::GROUP_TIP);
    ui->label->setText(m_groupTitle);
}

GroupTipItem::~GroupTipItem()
{
    delete ui;
}

QSize GroupTipItem::sizeHint() const
{
    return {250, 25};
}

void GroupTipItem::setGroupTitle(const QString& title)
{
    m_groupTitle = title;
    ui->label->setText(m_groupTitle);
}
