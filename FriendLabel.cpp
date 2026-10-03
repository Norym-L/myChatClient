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

	// 尺寸常量与 FriendLabel.ui / friendlabel.qss 三方对应，改动需同步：
	//
	//   胶囊总宽 = 文字宽 + 关闭图标宽 + 14
	//     14 = 布局左/右外边距 4+4、文字与图标间距 4、QSS 描边 1+1
	//   胶囊总高 = 文字行高 + 14
	//     14 = 上下内边距各 6、QSS 描边 1+1
	//
	// 旧值 +5 / +2 是缺陷根源：高度只有 text_height+2（12px 字体下实测 14px），
	// 没有上下留白，且宽度漏算了布局外边距与描边，导致关闭图标被裁掉。
	setFixedWidth(ui->tip_label->width() + ui->close_label->width() + 14);
	setFixedHeight(text_height + 14);

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

