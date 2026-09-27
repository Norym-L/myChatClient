#include "chatitembase.h"
#include "ui_ChatItemBase.h"

ChatItemBase::ChatItemBase(const ChatRole role, QWidget* parent)
    : QWidget(parent), m_role(role), ui(new Ui::ChatItemBase)
{
    ui->setupUi(this);
    m_widget = ui->message_widget;

    // 镜像区分自己、他人的消息
    ui->mainLayout->setDirection(m_role == ChatRole::USER_SLEF
                                     ? QBoxLayout::LeftToRight
                                     : QBoxLayout::RightToLeft);
}

ChatItemBase::~ChatItemBase()
{
    delete ui;
}

void ChatItemBase::setUserName(const QString& name) const
{
    ui->name->setText(name);
}

void ChatItemBase::setUserIcon(const QPixmap& icon) const
{
    ui->icon->setPixmap(icon);
}

void ChatItemBase::setWidget(QWidget* widget) {}
