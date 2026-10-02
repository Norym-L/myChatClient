#include "chatitembase.h"
#include "ui_chatitembase.h"

#include <QVBoxLayout>

ChatItemBase::ChatItemBase(const ChatRole role, QWidget* parent)
    : QWidget(parent), m_role(role), ui(new Ui::ChatItemBase)
{
    ui->setupUi(this);
    m_widget = ui->message_widget;

    // message_widget 在 .ui 中没有布局，这里补一个，供 setWidget 装入气泡
    auto* message_layout = new QVBoxLayout(m_widget);
    message_layout->setContentsMargins(0, 0, 0, 0);
    message_layout->setSpacing(0);

    // 气泡内部的 QTextEdit 呈 Expanding，会传导到 message_widget 上，
    // 使它跟 horizontalLayout_3 里的弹簧抢多余宽度：窗口一拉长
    // message_widget 就被撑宽，宽度受限的气泡便与之脱节。
    // 收紧为 Maximum 后，多余水平空间全部交给弹簧。
    m_widget->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    // 垂直方向同样收紧为 Maximum：条目高度只跟内容走，
    // 窗口拉高时多余空间交给消息列表的弹性占位，避免条目被
    // 拉高后气泡行跟着变高、名字与气泡之间出现空隙
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Maximum);

    ui->icon->setScaledContents(true);
    ui->icon->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    // 镜像区分自己、他人的消息
    if (m_role == ChatRole::USER_SLEF)
    {
        ui->horizontalLayout_2->setDirection(QBoxLayout::LeftToRight);
        ui->horizontalLayout_3->setDirection(QBoxLayout::LeftToRight);
        ui->mainLayout->setDirection(QBoxLayout::LeftToRight);
    }
    else if (m_role == ChatRole::OTHER)
    {
        ui->horizontalLayout_2->setDirection(QBoxLayout::RightToLeft);
        ui->horizontalLayout_3->setDirection(QBoxLayout::RightToLeft);
        ui->mainLayout->setDirection(QBoxLayout::RightToLeft);
    }
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

void ChatItemBase::setWidget(QWidget* widget) const
{
    if (widget == nullptr)
    {
        return;
    }
    auto* layout = m_widget->layout();
    if (layout == nullptr || layout->count() > 0) // 已装入过则不重复添加
    {
        return;
    }
    // 气泡宽度按文本收紧后可能小于所在 cell，显式贴向本角色一侧，
    // 保证窗口拉伸缩放后气泡始终与名字/头像对齐（镜像角色自动对左侧）
    const auto align = (m_role == ChatRole::USER_SLEF) ? Qt::AlignRight : Qt::AlignLeft;
    // addWidget 会接管 widget 的 parent，气泡随条目一起显示
    layout->addWidget(widget);
    layout->setAlignment(widget, align);
}
