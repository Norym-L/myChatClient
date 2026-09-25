#include "ui/components/timer_button.h"

#include "core/logger.h"

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
				this->setText(tr("获取验证码"));
				this->setEnabled(true);
				return;
			}
			this->setText(tr("%1 秒后重新获取验证码").arg(m_counter));
		});

	// 冷却挂在 clicked 上：基类已在 mouseReleaseEvent 里完整走完
	// "按下状态判定 -> 发出 clicked"，此时再禁用按钮不会影响本次 clicked 的发出。
	connect(this, &QPushButton::clicked, this, [this]()
		{
			LOG_DEBUG() << "[ui] timer button clicked, start countdown";
			startCountdown();
		});
}

TimerButton::~TimerButton()
{
	m_timer->stop();
}

void TimerButton::startCountdown()
{
	m_counter = 10;
	this->setEnabled(false);
	this->setText(tr("%1 秒后重新获取验证码").arg(m_counter));
	m_timer->start(1000);
}
