#pragma once

#include <QPushButton>
#include <QTimer>

/**
 * 带冷却倒计时的按钮（用于"获取验证码"）
 *
 * 注意：冷却必须在 clicked 信号里启动，不能在 mouseReleaseEvent 中提前 setEnabled(false)。
 * 禁用按钮会清掉 QAbstractButton 内部的 down 状态，基类的 mouseReleaseEvent 随即提前
 * return，clicked() 永远不会发出，按钮表现为"点了没反应"。
 */
class TimerButton : public QPushButton
{
public:
	explicit TimerButton(QWidget* parent = nullptr);
	~TimerButton() override;

private:
	/// 开始冷却倒计时
	void startCountdown();

	QTimer* m_timer = nullptr;
	int m_counter = 10;
};
