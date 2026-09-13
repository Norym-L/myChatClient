#pragma once

#include <QPushButton>
#include <QTimer>

class TimerButton : public QPushButton
{
public:
	TimerButton(QWidget* parent = nullptr);
	~TimerButton() override;

	void mouseReleaseEvent(QMouseEvent* e) override;

private:
	QTimer* m_timer;
	int m_counter;
};

