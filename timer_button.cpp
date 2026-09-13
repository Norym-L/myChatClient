#include "timer_button.h"
#include <QMouseEvent>

TimerButton::TimerButton(QWidget* parent)
	: QPushButton(parent), m_counter(10)
{
	m_timer = new QTimer(this);

	connect(m_timer, &QTimer::timeout, [this]()
		{
			m_counter--;
			if (m_counter <= 0)
			{
				m_timer->stop();
				m_counter = 10;
				this->setText(tr("获取"));
				this->setEnabled(true);
				return;
			}
			this->setText(tr("%1 秒后重新获取验证码").arg(m_counter));
		});
}

TimerButton::~TimerButton()
{
	m_timer->stop();
}

void TimerButton::mouseReleaseEvent(QMouseEvent* e)
{
	if (e->button() == Qt::LeftButton)
	{
		qDebug() << "释放获取验证码按钮\n";
		this->setEnabled(false);
		this->setText(tr("%1 秒后重新获取验证码").arg(m_counter));
		m_timer->start(1000);
		emit clicked();
	}

	QPushButton::mouseReleaseEvent(e);
}
