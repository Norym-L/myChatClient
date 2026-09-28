#include "interchatpage.h"
#include "ui_interchatpage.h"

#include <QStyleOption>
#include <QPainter>
#include <QEvent>

#include "chatItem/TextBubble.h"
#include "chatItem/PictureBubble.h"
#include "app/global.h"
#include "chatItem/chatitembase.h"
#include "core/logger.h"

InterChatPage::InterChatPage(QWidget* parent)
    : QWidget(parent), ui(new Ui::InterChatPage)
{
    ui->setupUi(this);
#ifdef CLIENT_DEBUG
#else
    ui->receive_button->hide();
#endif

    initChatInputEditAndRecvSendButton();
}

InterChatPage::~InterChatPage()
{
    delete ui;
}

void InterChatPage::paintEvent(QPaintEvent* event)
{
    // 使样式表生效
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}

void InterChatPage::initChatInputEditAndRecvSendButton()
{
    // 设置按钮悬浮在输入框上的样式
    // 按钮区脱离页面布局，改挂到输入框之下，手动摆到右下角。
    // 不放入布局，避免它被拉伸成整块而拦掉输入框的鼠标事件。
    ui->verticalLayout->removeWidget(ui->button_widget);
    ui->button_widget->setParent(ui->chat_edit);
    ui->button_widget->raise();

    // 输入框尺寸变化（窗口缩放）时重新摆放
    ui->chat_edit->installEventFilter(this);
    updateButtonPos();

    // 按钮样式
    ui->receive_button->setState("normal", "hover", "press");
    ui->send_button->setState("normal", "hover", "press");

    // 快捷栏样式
    ui->emoji_label->setState("normal", "hover", "press", "normal", "hover", "press");
    ui->file_label->setState("normal", "hover", "press", "normal", "hover", "press");

    // 连接发送事件
    connect(ui->send_button, &ClickedButton::released, this, &InterChatPage::slotSendButtonPushed);
}

bool InterChatPage::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == ui->chat_edit && event->type() == QEvent::Resize)
    {
        updateButtonPos();
    }

    return QWidget::eventFilter(watched, event);
}

void InterChatPage::updateButtonPos() const
{
    constexpr int kMargin = 28;
    const QSize hint = ui->button_widget->sizeHint();

    ui->button_widget->setGeometry(ui->chat_edit->width() - hint.width() - kMargin,
                                   ui->chat_edit->height() - hint.height() - kMargin,
                                   hint.width(),
                                   hint.height());
}

void InterChatPage::slotSendButtonPushed() const
{
    const QString user_name = "norym";
    const QString icon_url = ":/assets/images/head_4.jpg";
    constexpr auto role = ChatRole::USER_SLEF;

    const auto text_edit = ui->chat_edit;
    const QVector<MessageInfo>& message_list = text_edit->getMessageList();

    for (auto i = 0; i < message_list.length(); ++i)
    {
        auto type = message_list[i].messageFlag;
        auto* chat_item = new ChatItemBase(role);
        chat_item->setUserName(user_name);
        chat_item->setUserIcon(QPixmap(icon_url));
        QWidget* bubble = nullptr;
        switch (type)
        {
            case MessageInfo::MessageFlag::TEXT:
            {
                bubble = new TextBubble(role, message_list[i].content);
                break;
            }
            case MessageInfo::MessageFlag::IMAGE:
            {
                bubble = new PictureBubble(role, message_list[i].content);
                break;
            }
            case MessageInfo::MessageFlag::FILE:
            {
                // TODO
                break;
            }
            default:
            {
                LOG_WARN() << "[chat page][send button] unknow message type!";
                break;
            }
        }
        if (bubble != nullptr)
        {
            chat_item->setWidget(bubble);
            ui->chat_data_list->appendChatItemAtTail(chat_item);
        }
    }
}
