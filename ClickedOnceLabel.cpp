#include "ClickedOnceLabel.h"

#include <QMouseEvent>

#include "logger.h"

ClickedOnceLabel::ClickedOnceLabel(QWidget* parent)
	: QLabel(parent)
{
	setCursor(Qt::PointingHandCursor);
}

void ClickedOnceLabel::setEnable(const bool enable)
{
	m_enable = enable;
}

bool ClickedOnceLabel::isEnable() const
{
	return m_enable;
}

void ClickedOnceLabel::mousePressEvent(QMouseEvent* ev)
{
	if (ev->button() == Qt::LeftButton)
	{
		emit clicked(text());
		return;
	}
	QLabel::mousePressEvent(ev);
}

void ClickedOnceLabel::mouseReleaseEvent(QMouseEvent* ev)
{
	if (ev->button() == Qt::LeftButton)
	{
		emit released(text());
		return;
	}

	QLabel::mouseReleaseEvent(ev);
}
