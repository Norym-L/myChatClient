#include "ApplyFriendItem.h"

ApplyFriendItem::ApplyFriendItem(QWidget* parent)
    : ListItemBase(parent), ui(new Ui::ApplyFriendItemClass())
{
    ui->setupUi(this);
    setItemType(ListItemType::APPLY_FRIEND);
    ui->accept_friend_apply_button->setState("normal", "hover", "press");

    showAddButton(false);
    connect(ui->accept_friend_apply_button, &ClickedButton::released, [this]() { emit sigAuthFriend(m_apply_info); });
}

ApplyFriendItem::~ApplyFriendItem()
{
    delete ui;
}

void ApplyFriendItem::setInfo(std::shared_ptr<ApplyInfo> apply_info)
{
    m_apply_info = std::move(apply_info);

    const QPixmap pixmap(m_apply_info->icon);
    ui->icon->setPixmap(pixmap.scaled(ui->icon->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->icon->setScaledContents(true);
    ui->user_name->setText(m_apply_info->name);
    ui->user_info->setText(m_apply_info->desc);
}

void ApplyFriendItem::setInfo(std::shared_ptr<AddFriendApply> apply)
{
    m_apply_info = std::make_shared<ApplyInfo>(std::move(apply));

    const QPixmap pixmap(m_apply_info->icon);
    ui->icon->setPixmap(pixmap.scaled(ui->icon->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    ui->icon->setScaledContents(true);
    ui->user_name->setText(m_apply_info->name);
    ui->user_info->setText(m_apply_info->desc);
}

std::shared_ptr<ApplyInfo> ApplyFriendItem::getInfo() const
{
    return m_apply_info;
}

void ApplyFriendItem::setTipText(const QString& text) const
{
    ui->tip_label->setText(text);
}

void ApplyFriendItem::showAddButton(const bool show)
{
    if (show)
    {
        ui->accept_friend_apply_button->show();
        ui->tip_label->hide();
        m_beAdded = false;
    }
    else
    {
        ui->accept_friend_apply_button->hide();
        ui->tip_label->show();
        m_beAdded = true;
    }
}

QSize ApplyFriendItem::sizeHint() const
{
    return {250, 80};
}

std::uint64_t ApplyFriendItem::getUid() const
{
    return m_apply_info->from_uid;
}
