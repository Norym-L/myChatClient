#include "clicked_label.h"

#include <QMouseEvent>

ClickedLabel::ClickedLabel(QWidget* parent)
	: QLabel(parent), m_currentState(ClickLabelState::NORMAL)
{}

void ClickedLabel::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		if (m_currentState == ClickLabelState::NORMAL)
		{
			qDebug() << "clicked, change to normal pressed: " << m_normalPress;
			setProperty("state", m_normalPress);
		}
		else
		{
			qDebug() << "clicked, change to select pressed: " << m_selectedPress;
			setProperty("state", m_selectedPress);
		}

		emit clicked();
		repolish(this);
		update();
	}
	QLabel::mousePressEvent(event);
}

void ClickedLabel::mouseReleaseEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		if (m_currentState == ClickLabelState::NORMAL)
		{
			qDebug() << "released, change to select hover: " << m_selectedHover;
			m_currentState = ClickLabelState::SELECT;
			setProperty("state", m_selectedHover);
		}
		else
		{
			qDebug() << "released, change to normal hover: " << m_normalHover;
			m_currentState = ClickLabelState::NORMAL;
			setProperty("state", m_normalHover);
		}

		emit released();
		repolish(this);
		update();
	}
	QLabel::mouseReleaseEvent(event);
}

void ClickedLabel::enterEvent(QEnterEvent* event)
{
	if (m_currentState == ClickLabelState::NORMAL)
	{
		qDebug() << "enter, change to normal hover:" << m_normalHover;
		setProperty("state", m_normalHover);
	}
	else
	{
		qDebug() << "enter, change to selected hover:" << m_selectedHover;
		setProperty("state", m_selectedHover);
	}

	repolish(this);
	update();

	QLabel::enterEvent(event);
}

void ClickedLabel::leaveEvent(QEvent* event)
{
	if (m_currentState == ClickLabelState::NORMAL)
	{
		qDebug() << "enter, change to normal:" << m_normal;
		setProperty("state", m_normal);
	}
	else
	{
		qDebug() << "enter, change to selected:" << m_selected;
		setProperty("state", m_selected);
	}

	repolish(this);
	update();

	QLabel::leaveEvent(event);
}

void ClickedLabel::setState(const QString& normal, const QString& normal_hover, const QString& normal_press, const QString& select, const QString& select_hover, const QString& select_press)
{
	m_normal = normal;
	m_normalHover = normal_hover;
	m_normalPress = normal_press;
	m_selected = normal;
	m_selectedHover = normal_hover;
	m_selectedPress = normal_press;

	setProperty("state", m_normal);
	repolish(this);
}


ClickLabelState ClickedLabel::getCurrentState() const
{
	return m_currentState;
}
