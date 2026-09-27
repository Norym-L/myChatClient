#include "interchatpage.h"
#include "ui_interchatpage.h"

#include <QStyleOption>
#include <QPainter>
#include <QEvent>

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
