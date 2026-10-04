#include "GroupTipItem.h"

GroupTipItem::GroupTipItem(QWidget *parent)
	: QWidget(parent), ui(new Ui::GroupTipItemClass())
{
	ui->setupUi(this);
}

GroupTipItem::GroupTipItem(const QString& title, QWidget* parent)
	: QWidget(parent), ui(new Ui::GroupTipItemClass())
{
	ui->setupUi(this);
	ui->label->setText(title);
}

GroupTipItem::~GroupTipItem()
{
	delete ui;
}

