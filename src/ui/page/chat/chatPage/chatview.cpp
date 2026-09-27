#include "chatview.h"

#include <QTimer>
#include <QEvent>
#include <QStyleOption>
#include <QPainter>
#include <QScrollBar>

#include "ui_ChatView.h"


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

bool ChatView::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::Enter && watched == ui->chat_area)
    {
        ui->chat_area->verticalScrollBar()->setHidden(ui->chat_area->verticalScrollBar()->maximum() == 0);
    }
    else if (event->type() == QEvent::Leave && watched == ui->chat_area)
    {
        ui->chat_area->verticalScrollBar()->setHidden(true);
    }
    return QWidget::eventFilter(watched, event);
}

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
    ui->chat_area->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* scroll_bar = ui->chat_area->verticalScrollBar();
    // 连接滚动处理函数
    connect(scroll_bar, &QScrollBar::rangeChanged, this, &ChatView::slotOnVScrollBarMoved);

    auto* scroll_area = ui->chat_area;

    // 悬浮滚动条
    auto* qHLayout = new QHBoxLayout();
    qHLayout->addWidget(scroll_bar, 0, Qt::AlignRight);
    qHLayout->setContentsMargins(QMargins(0, 0, 0, 0));
    scroll_area->setLayout(qHLayout);
    scroll_bar->setHidden(true);

    scroll_area->setWidgetResizable(true);
    scroll_area->installEventFilter(this);
}

void ChatView::initStyleSheet() {}
