#include "friendwidget.h"
#include "ui_FriendWidget.h"


FriendWidget::FriendWidget(QWidget* parent)
: ListItemBase(parent), ui(new Ui::FriendWidget)
{
    ui->setupUi(this);
    setItemType(ListItemType::CHAT_USER);
}

FriendWidget::~FriendWidget()
{
    delete ui;
}

QSize FriendWidget::sizeHint() const
{
    return {250, 70};
}

void FriendWidget::setInfo(const QString& name, const QString& icon, const QString& message)
{
    m_name = name;
    m_icon = icon;
    m_message = message;

    const QPixmap pixmap(m_icon); // 加载图片，自动缩放
    ui->icon->setPixmap(pixmap.scaled(ui->icon->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->icon->setScaledContents(true);

    ui->user_name->setText(m_name);
    ui->user_chat->setText(m_message);
}
