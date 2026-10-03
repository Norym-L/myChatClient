#include "FriendLabel.h"

#include <QFontMetrics>

FriendLabel::FriendLabel(QWidget *parent)
	: QFrame(parent), ui(new Ui::FriendLabelClass())
{
	ui->setupUi(this);
	ui->close_label->setState("normal", "hover", "pressed", "selected_normal", "selected_hover", "selected_pressed");
	connect(ui->close_label, &ClickedLabel::released, this, &FriendLabel::slotClose);
}

FriendLabel::~FriendLabel()
{
	delete ui;
}

void FriendLabel::setText(const QString& text)
{
	m_text = text;
	ui->tip_label->setText(m_text);
	ui->tip_label->adjustSize();

	const QFontMetrics font_metrics(ui->tip_label->font());
	const auto text_width = font_metrics.horizontalAdvance(m_text);
	const auto text_height = font_metrics.height();

	setFixedWidth(ui->tip_label->width() + ui->close_label->width() + 5);
	setFixedHeight(text_height + 2);

	m_width = width();
	m_height = height();
}

int FriendLabel::getWidth() const
{
	return m_width;
}

int FriendLabel::getHeight() const
{
	return m_height;
}

QString FriendLabel::text() const
{
	return m_text;
}

void FriendLabel::slotClose() 
{
	emit sigClose(m_text);
}

