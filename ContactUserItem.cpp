#include "ContactUserItem.h"

ContactUserItem::ContactUserItem(QWidget *parent)
	: ListItemBase(parent), ui(new Ui::ContactUserItemClass())
{
	ui->setupUi(this);
	setItemType(ListItemType::CONTACT_USER);
	ui->red_point->raise(); // 🔴展示在最前面
	showRedPoint(false); // 默认不显示红点
}

ContactUserItem::~ContactUserItem()
{
	delete ui;
}

QSize ContactUserItem::sizeHint() const
{
	return { 250, 70 };
}

void ContactUserItem::setInfo(const std::shared_ptr<AuthInfo>& auth_info)
{
	m_info = std::make_shared<UserInfo>(auth_info);
	const QPixmap pixmap(m_info->icon);
	ui->icon->setPixmap(pixmap.scaled(ui->icon->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
	ui->icon->setScaledContents(true);
	ui->user_name->setText(m_info->name);
}

// void ContactUserItem::setInfo(const std::shared_ptr<AuthRsp>& auth_rsp)
// {
// 	m_info = std::make_shared<UserInfo>(auth_rsp);
// 	const QPixmap pixmap(m_info->icon);
// 	ui->icon->setPixmap(pixmap.scaled(ui->icon->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
// 	ui->icon->setScaledContents(true);
// 	ui->user_name->setText(m_info->name);
// }

void ContactUserItem::setInfo(std::uint64_t uid, const QString& name, const QString& icon, const QString& nick)
{
	m_info = std::make_shared<UserInfo>(uid, name, icon, nick);
	const QPixmap pixmap(m_info->icon);
	ui->icon->setPixmap(pixmap.scaled(ui->icon->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
	ui->icon->setScaledContents(true);
	ui->user_name->setText(m_info->name);
}

void ContactUserItem::showRedPoint(const bool show) const
{
	if (show)
	{
		ui->red_point->show();
	}
	else
	{
		ui->red_point->hide();
	}
}

