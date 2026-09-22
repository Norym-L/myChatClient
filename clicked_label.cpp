#include "clicked_label.h"

#include <QMouseEvent>

#include "logger.h"

ClickedLabel::ClickedLabel(QWidget* parent)
	: QLabel(parent), m_currentState(ClickLabelState::NORMAL)
{}

void ClickedLabel::mousePressEvent(QMouseEvent* event)
{
	if (event->button() == Qt::LeftButton)
	{
		if (m_normalPress.isEmpty() || m_selectedPress.isEmpty())
		{
			return;
		}

		if (m_currentState == ClickLabelState::NORMAL)
		{
			LOG_DEBUG() << "[ui] clicked label pressed, normal state:" << m_normalPress;
			setProperty("state", m_normalPress);
		}
		else
		{
			LOG_DEBUG() << "[ui] clicked label pressed, select state:" << m_selectedPress;
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
		if (m_normalHover.isEmpty() || m_selectedHover.isEmpty())
		{
			return;
		}

		if (m_currentState == ClickLabelState::NORMAL)
		{
			LOG_DEBUG() << "[ui] clicked label released, select hover:" << m_selectedHover;
			m_currentState = ClickLabelState::SELECT;
			setProperty("state", m_selectedHover);
		}
		else
		{
			LOG_DEBUG() << "[ui] clicked label released, normal hover:" << m_normalHover;
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
	if (m_normalHover.isEmpty() || m_selectedHover.isEmpty())
	{
		return;
	}

	if (m_currentState == ClickLabelState::NORMAL)
	{
		LOG_DEBUG() << "[ui] clicked label entered, normal hover:" << m_normalHover;
		setProperty("state", m_normalHover);
	}
	else
	{
		LOG_DEBUG() << "[ui] clicked label entered, select hover:" << m_selectedHover;
		setProperty("state", m_selectedHover);
	}

	repolish(this);
	update();

	QLabel::enterEvent(event);
}

void ClickedLabel::leaveEvent(QEvent* event)
{
	if (m_normal.isEmpty() || m_selected.isEmpty())
	{
		return;
	}

	if (m_currentState == ClickLabelState::NORMAL)
	{
		LOG_DEBUG() << "[ui] clicked label leaved, normal:" << m_normal;
		setProperty("state", m_normal);
	}
	else
	{
		LOG_DEBUG() << "[ui] clicked label leaved, select:" << m_selected;
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
