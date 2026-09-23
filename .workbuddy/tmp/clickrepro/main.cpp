// 临时复现程序（不属于工程源码，验证完即删）
// 目的：
//   1) 真实点击 TimerButton（"获取验证码"）时，clicked() 是否会发出；
//   2) RegisterDialog 中 confirm_button 的 clicked 是否真的连到了 handleGetVerify。
#include <QApplication>
#include <QDebug>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPushButton>

#include "registerdialog.h"
#include "timer_button.h"

static int g_timerButtonClicked = 0;

static void sendClick(QWidget* w)
{
	w->show();
	const QPointF local(w->width() / 2.0, w->height() / 2.0);
	const QPointF global(w->mapToGlobal(local.toPoint()));
	QMouseEvent press(QEvent::MouseButtonPress, local, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
	QApplication::sendEvent(w, &press);
	QMouseEvent release(QEvent::MouseButtonRelease, local, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
	QApplication::sendEvent(w, &release);
	qDebug().noquote() << "  [sendClick] 已发送 左键 按下+释放 到" << w->objectName() << "几何:" << w->size();
}

int main(int argc, char* argv[])
{
	QApplication app(argc, argv);

	qDebug().noquote() << "===== 实验 1：裸 TimerButton 的 clicked() =====";
	{
		TimerButton btn;
		btn.setObjectName("bare_timer_button");
		btn.resize(200, 30);
		QObject::connect(&btn, &QPushButton::clicked, [](bool) { g_timerButtonClicked++; });
		qDebug().noquote() << "  点击前 enabled =" << btn.isEnabled() << " text =" << btn.text();
		sendClick(&btn);
		qDebug().noquote() << "  点击后 enabled =" << btn.isEnabled() << " text =" << btn.text()
			<< " clicked 次数 =" << g_timerButtonClicked;
	}

	qDebug().noquote() << "";
	qDebug().noquote() << "===== 实验 2：RegisterDialog 里的真实按钮 =====";
	{
		RegisterDialog dlg;
		dlg.resize(350, 500);
		dlg.show();

		auto* email_edit = dlg.findChild<QLineEdit*>("email_edit");
		auto* token_edit = dlg.findChild<QLineEdit*>("token_edit");
		auto* confirm_button = dlg.findChild<QPushButton*>("confirm_button");
		auto* cancel_button = dlg.findChild<QPushButton*>("cancel_button");
		auto* get_token_button = dlg.findChild<QPushButton*>("get_token_button");
		auto* err_tip = dlg.findChild<QLabel*>("err_tip");

		if (!email_edit || !confirm_button || !get_token_button || !err_tip)
		{
			qDebug().noquote() << "  [FATAL] 控件查找失败";
			return 2;
		}

		qDebug().noquote() << "  ui 指针是否可用（能取到控件）: 是";
		qDebug().noquote() << "  get_token_button 实际类名:" << get_token_button->metaObject()->className()
			<< " 文本:" << get_token_button->text();
		qDebug().noquote() << "  confirm_button 文本:" << confirm_button->text();

		int realClicked = 0;
		QObject::connect(get_token_button, &QPushButton::clicked, &dlg, [&realClicked]() { realClicked++; });

		qDebug().noquote() << "";
		qDebug().noquote() << "  --- 2a：点击【获取验证码】按钮 ---";
		email_edit->setText("someone@example.com");
		qDebug().noquote() << "  点击前: get_token_button enabled =" << get_token_button->isEnabled()
			<< " err_tip =" << err_tip->text();
		sendClick(get_token_button);
		qDebug().noquote() << "  点击后: clicked 次数 =" << realClicked
			<< " get_token_button enabled =" << get_token_button->isEnabled()
			<< " text =" << get_token_button->text();
		qDebug().noquote() << "  点击后: err_tip =" << err_tip->text();

		qDebug().noquote() << "";
		qDebug().noquote() << "  --- 2b：点击【确认】按钮 ---";
		err_tip->clear();
		get_token_button->setEnabled(true);
		qDebug().noquote() << "  点击前: err_tip 已清空, get_token_button enabled =" << get_token_button->isEnabled();
		sendClick(confirm_button);
		qDebug().noquote() << "  点击后: err_tip =" << err_tip->text();
		qDebug().noquote() << "  点击后: get_token_button enabled =" << get_token_button->isEnabled()
			<< "（若变为 false，说明 handleGetVerify 被执行了）";
		qDebug().noquote() << "  点击后: confirm_button enabled =" << confirm_button->isEnabled();
	}

	return 0;
}
