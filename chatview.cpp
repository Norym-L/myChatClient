#include "chatview.h"

#include <QTimer>
#include <QStyleOption>
#include <QPainter>
#include <QScrollBar>

#include "ui_chatview.h"


ChatView::ChatView(QWidget* parent)
    : QWidget(parent), ui(new Ui::ChatView)
{
    ui->setupUi(this);

    initScrollBar();
    initStyleSheet();
}

ChatView::~ChatView()
{
    delete ui;
}

void ChatView::appendChatItemAtTail(QWidget* item)
{
    auto* vl = qobject_cast<QVBoxLayout*>(ui->widget->layout());
    vl->insertWidget(vl->count() - 1, item);
    m_isAppendedChatItem = true;
}

void ChatView::appendChatItemAtHead(QWidget* item)
{}

void ChatView::insertChatItem(QWidget* befor, QWidget* item)
{}

void ChatView::paintEvent(QPaintEvent* event)
{
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
}

void ChatView::slotOnVScrollBarMoved(int min, int max)
{
    if (m_isAppendedChatItem) // 添加多次item会多次调用，需要500ms冷却
    {
        auto* scroll_bar = ui->chat_area->verticalScrollBar();
        scroll_bar->setSliderPosition(scroll_bar->maximum());
        QTimer::singleShot(
            500,
            [this]()
                {
                    m_isAppendedChatItem = false;
                });
    }
}

void ChatView::initScrollBar()
{
    // 滚动条常驻占位：AsNeeded 下滚动条出现/消失会改变视口宽度，
    // 所有条目横向重排，视觉上"弹一下"；AlwaysOn 宽度恒定不重排。
    // （Qt Widgets 无原生 overlay 滚动条，"悬浮条"方案已在 Qt 6.11 实测不可行）
    ui->chat_area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);

    auto* scroll_bar = ui->chat_area->verticalScrollBar();
    // 连接滚动处理函数
    connect(scroll_bar, &QScrollBar::rangeChanged, this, &ChatView::slotOnVScrollBarMoved);
}

void ChatView::initStyleSheet() {}
