#include "bubbleframe.h"

#include <QPainter>
#include <QColor>

#include "ui_bubbleframe.h"

#define WIDTH_DERTA 6   // 聊天气包三角形的宽度

BubbleFrame::BubbleFrame(const ChatRole role, QWidget* parent)
    : QFrame(parent), m_role(role), ui(new Ui::BubbleFrame)
{
    ui->setupUi(this);
    setMargin(m_margin);
    // 供主题 QSS 区分自己/对方气泡内的文字色（TextBubble[bubbleRole=...]）
    setProperty("bubbleRole", m_role == ChatRole::USER_SLEF ? "self" : "other");
}

BubbleFrame::~BubbleFrame()
{
    delete ui;
}

void BubbleFrame::setMargin(const int margin)
{
    m_margin = margin;
    if (m_role == ChatRole::USER_SLEF)
        ui->mainLayout->setContentsMargins({m_margin, m_margin, m_margin + WIDTH_DERTA, m_margin});
    else
        ui->mainLayout->setContentsMargins({m_margin + WIDTH_DERTA, m_margin, m_margin, m_margin});
}

void BubbleFrame::setWidget(QWidget* widget) const
{
    if (ui->mainLayout->count() > 0)
    {
        return;
    }
    ui->mainLayout->addWidget(widget);
}

QMargins BubbleFrame::bubbleMargins() const
{
    return ui->mainLayout->contentsMargins();
}

void BubbleFrame::paintEvent(QPaintEvent* event)
{
    // 画气泡（颜色由主题 QSS 经 qproperty 注入，见 bubbleframe.h）
    QPainter painter(this);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_role == ChatRole::USER_SLEF ? m_bubbleColorSelf : m_bubbleColorOther);
    if (m_role == ChatRole::USER_SLEF)
    {
        const auto bk_rect = QRect(0, 0, this->width() - WIDTH_DERTA, this->height());
        painter.drawRoundedRect(bk_rect, 5, 5);
        const QPointF postions[] = {
            QPointF(bk_rect.x() + bk_rect.width(), 12),
            QPointF(bk_rect.x() + bk_rect.width(), 12 + WIDTH_DERTA + 2),
            QPointF(bk_rect.x() + bk_rect.width() + WIDTH_DERTA, 10 + WIDTH_DERTA / 2)
        };
        painter.drawPolygon(postions, 3);
    }
    else
    {
        const auto bk_rect = QRect(WIDTH_DERTA, 0, this->width() - WIDTH_DERTA, this->height());
        painter.drawRoundedRect(bk_rect, 5, 5);
        const QPointF postions[] = {
            QPointF(bk_rect.x(), 12),
            QPointF(bk_rect.x(), 12 + WIDTH_DERTA + 2),
            QPointF(bk_rect.x() - WIDTH_DERTA, 10 + WIDTH_DERTA / 2)
        };
        painter.drawPolygon(postions, 3);
    }

    QFrame::paintEvent(event);
}
