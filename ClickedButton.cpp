#include "ClickedButton.h"

#include "global.h"

ClickedButton::ClickedButton(QWidget* parent)
	: QPushButton(parent)
{
	setCursor(Qt::PointingHandCursor);
}

ClickedButton::~ClickedButton()
{
	(void) nullptr;
}

void ClickedButton::setState(const QString& normal, const QString& hover, const QString& press)
{
	m_normal = normal;
	m_hover = hover;
	m_press = press;
	setProperty("state", m_normal);
	repolish(this);
	update();
}

void ClickedButton::enterEvent(QEnterEvent* event)
{
	setProperty("state", m_hover);
	repolish(this);
	update();
	QPushButton::enterEvent(event);
}

void ClickedButton::leaveEvent(QEvent* event)
{
	setProperty("state", m_normal);
	repolish(this);
	update();
	QPushButton::leaveEvent(event);
}

void ClickedButton::mousePressEvent(QMouseEvent* e)
{
	setProperty("state", m_press);
	repolish(this);
	update();
	QPushButton::mousePressEvent(e);
}

void ClickedButton::mouseReleaseEvent(QMouseEvent* e)
{
	setProperty("state", m_hover);
	repolish(this);
	update();
	QPushButton::mouseReleaseEvent(e);
}
