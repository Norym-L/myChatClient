#pragma once

#include <QPushButton>
class ClickedButton : public QPushButton
{
public:
	explicit ClickedButton(QWidget* parent = nullptr);
	~ClickedButton() override;
	void setState(const QString& normal, const QString& hover, const QString& press);

protected:
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void mousePressEvent(QMouseEvent* e) override;
	void mouseReleaseEvent(QMouseEvent* e) override;

private:
	QString m_normal;
	QString m_hover;
	QString m_press;
};

