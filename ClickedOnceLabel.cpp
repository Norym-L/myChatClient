#include "ClickedOnceLabel.h"

#include <QMouseEvent>

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
		if (m_enable)
		{
			m_enable = false;
			emit clicked();
			return;
		}
	}
	QLabel::mousePressEvent(ev);
}

void ClickedOnceLabel::mouseReleaseEvent(QMouseEvent* ev)
{
	if (ev->button() == Qt::LeftButton)
	{
		if (m_enable)
		{
			m_enable = false;
			emit released();
			return;
		}
	}

	QLabel::mouseReleaseEvent(ev);
}
