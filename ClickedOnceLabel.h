#pragma once

#include <QLabel>

class ClickedOnceLabel : public QLabel
{
	Q_OBJECT

public:
	explicit ClickedOnceLabel(QWidget* parent);

	void setEnable(bool enable);
	[[nodiscard]] bool isEnable() const;

protected:
	void mousePressEvent(QMouseEvent* ev) override;
	void mouseReleaseEvent(QMouseEvent* ev) override;

signals:
	void clicked();
	void released();

private:
	bool m_enable{ true };
};
