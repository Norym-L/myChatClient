#include "StateWidget.h"

#include <QMouseEvent>
#include <QVBoxLayout>
#include <QPainter>
#include <QStyleOption>

StateWidget::StateWidget(QWidget* parent)
    : QWidget(parent), m_currentState(ClickLabelState::NORMAL), m_redPoint(nullptr)
{
    setState();

    setCursor(Qt::PointingHandCursor);
    // 添加红点不显示
    addRedPoint();
}

void StateWidget::setState(
    const QString& normal, const QString& normal_hover, const QString& normal_pressed,
    const QString& selected, const QString& selected_hover, const QString& selected_pressed)
{
    m_normal = normal;
    m_normalHover = normal_hover;
    m_normalPressed = normal_pressed;
    m_selected = selected;
    m_selectedHover = selected_hover;
    m_selectedPressed = selected_pressed;

    setCurrentState();
}

ClickLabelState StateWidget::getCurrentState() const
{
    return m_currentState;
}

void StateWidget::clearState()
{
    m_currentState = ClickLabelState::NORMAL;
    setProperty("state", m_normal);
    repolish(this);
    update();
}

void StateWidget::setSelected(const bool selected)
{
    m_currentState = selected ? ClickLabelState::SELECT : ClickLabelState::NORMAL;
    setProperty("state", selected ? m_selected : m_normal);
    repolish(this);
    update();
}

void StateWidget::addRedPoint()
{
    m_redPoint = new QLabel();
    m_redPoint->setObjectName("redPoint");
    const auto layout2 = new QVBoxLayout;
    m_redPoint->setAlignment(Qt::AlignCenter);
    layout2->addWidget(m_redPoint);
    layout2->setContentsMargins(0, 0, 0, 0);
    layout2->setSpacing(0);
    setLayout(layout2);
    m_redPoint->setPixmap(QPixmap(":/assets/images/badge-red-16.png"));
    m_redPoint->setAlignment(Qt::AlignRight | Qt::AlignTop);
    m_redPoint->setVisible(false);
}

void StateWidget::showRedPoint(const bool show) const
{
    m_redPoint->setVisible(show);
}

void StateWidget::setCurrentState(const ClickLabelState state)
{
    m_currentState = state;
    if (m_currentState == ClickLabelState::SELECT)
    {
        setProperty("state", m_selected);
    }
    else
    {
        setProperty("state", m_normal);
    }
    repolish(this);
    update();
}

void StateWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        if (m_currentState == ClickLabelState::SELECT)
        {
            emit clickedOnSelected();
            QWidget::mousePressEvent(event);
            return;
        }
        if (m_currentState == ClickLabelState::NORMAL)
        {
            emit clicked();
            m_currentState = ClickLabelState::SELECT;
            setProperty("state", m_selectedPressed);
            repolish(this);
            update();
        }
        return;
    }
    QWidget::mousePressEvent(event);
}

void StateWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
    {
        emit released();
        if (m_currentState == ClickLabelState::NORMAL)
        {
            setProperty("state", m_normalHover);
            repolish(this);
            update();
        }
        else
        {
            setProperty("state", m_selectedHover);
            repolish(this);
            update();
        }
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void StateWidget::enterEvent(QEnterEvent* event)
{
    if (m_currentState == ClickLabelState::NORMAL)
    {
        setProperty("state", m_normalHover);
    }
    else
    {
        setProperty("state", m_selectedHover);
    }
    repolish(this);
    update();
    QWidget::enterEvent(event);
}

void StateWidget::leaveEvent(QEvent* event)
{
    if (m_currentState == ClickLabelState::NORMAL)
    {
        setProperty("state", m_normal);
    }
    else
    {
        setProperty("state", m_selected);
    }
    repolish(this);
    update();
    QWidget::leaveEvent(event);
}

void StateWidget::paintEvent(QPaintEvent* event)
{
    QStyleOption opt;
    opt.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &opt, &p, this);
}
