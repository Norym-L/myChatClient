#include "ApplyFriend.h"

#include <QScrollBar>

#include "UserManager.h"

ApplyFriend::ApplyFriend(QWidget* parent)
	: QDialog(parent), ui(new Ui::ApplyFriendClass())
{
	ui->setupUi(this);
	setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
	setObjectName("ApplyFriend");
	setModal(true);

	ui->verify_info_edit->setPlaceholderText(tr("我是norym")); // 自定义验证消息，在setSerachInfo中设置
	ui->back_name_edit->setPlaceholderText(tr("曹世明")); //默认展示对方用户名，在setSerachInfo中设置
	ui->label_edit->setPlaceholderText(tr("添加、搜索标签"));

	ui->label_edit->setMaxLen(30);
	ui->label_edit->move(2, 2);
	ui->label_edit->setFixedHeight(20);
	ui->label_edit->setMaxLength(10);
	ui->input_tip->hide();

	m_currentTipPoint = {5, 5};
	m_tipdata = {"同学", "家人", "cpp"};

	initTipLabel();
	connect(ui->more_label, &ClickedOnceLabel::released, this, &ApplyFriend::slotShwoMoreLabel);
	connect(ui->label_edit, &TruncateEdit::returnPressed, this, &ApplyFriend::slotLabelEnter);
	connect(ui->label_edit, &TruncateEdit::textChanged, this, &ApplyFriend::slotLabelTextChange);
	connect(ui->label_edit, &TruncateEdit::editingFinished, this, &ApplyFriend::slotLabelEditFinished);
	connect(ui->tip_label, &ClickedOnceLabel::released, this, &ApplyFriend::slotAddFriendLabelByClickTip);

	ui->scrollArea->horizontalScrollBar()->setHidden(true);
	ui->scrollArea->verticalScrollBar()->setHidden(true);
	ui->scrollArea->installEventFilter(this);
	ui->sure_button->setState("normal", "hover", "released");
	ui->cancel_button->setState("normal", "hover", "released");

	connect(ui->sure_button, &ClickedButton::released, this, &ApplyFriend::slotApplySure);
	connect(ui->cancel_button, &ClickedButton::released, this, &ApplyFriend::slotApplyCancel);
}

ApplyFriend::~ApplyFriend()
{
	delete ui;
}

void ApplyFriend::initTipLabel()
{}

void ApplyFriend::addTipLabel(ClickedLabel* label, QPoint current_point, QPoint& next_point, int text_width,
                              int text_height)
{}

void ApplyFriend::setSerachInfo(const std::shared_ptr<SearchInfo>& si)
{
	m_searchInfo = si;
	ui->verify_info_edit->setText("我是 " + UserManager::GetInstance().getName()); // 获取自己的名字
	ui->back_name_edit->setText(si->userName);
}

bool ApplyFriend::eventFilter(QObject* object, QEvent* event)
{
	if (object == ui->scrollArea && event->type() == QEvent::Enter)
	{
		ui->scrollArea->verticalScrollBar()->setHidden(false);
	}
	if (object == ui->scrollArea && event->type() == QEvent::Leave)
	{
		ui->scrollArea->verticalScrollBar()->setHidden(true);
	}
	return QDialog::eventFilter(object, event);
}

void ApplyFriend::resetLabel()
{}

void ApplyFriend::addLabel(const QString& name)
{
	if (m_friendLabels.find(name) != m_friendLabels.end())
	{
		return;
	}
	auto tmp_label = new FriendLabel(ui->grid);
	tmp_label->setText(name);
	tmp_label->setObjectName("friendLabel");

	auto max_width = ui->grid->width();
	if (m_labelPoint.x() + tmp_label->width() > max_width)
	{
		m_labelPoint.setX(2); // 换行
		m_labelPoint.setY(m_labelPoint.y() + tmp_label->height() + 6);
	}
	tmp_label->move(m_labelPoint);
	tmp_label->show();
	m_friendLabels[tmp_label->text()] = tmp_label;
	m_friendLabelKeys.push_back(tmp_label->text());

	connect(tmp_label, &FriendLabel::sigClose, this, &ApplyFriend::slotRemoveFriendLabel);

	m_labelPoint.setX(m_labelPoint.x() + tmp_label->width() + 2);
	if (m_labelPoint.x() + MIN_APPLY_LABEL_END_LEN > ui->grid->width())
	{
		ui->label_edit->move(2, m_labelPoint.y() + tmp_label->height() + 2);
	}
	else
	{
		ui->label_edit->move(m_labelPoint);
	}
}

void ApplyFriend::slotShwoMoreLabel()
{}

void ApplyFriend::slotLabelEnter()
{
	if (ui->label_edit->text().isEmpty())
	{
		return;
	}

	addLabel(ui->label_edit->text());
	ui->input_tip->hide();
}

void ApplyFriend::slotRemoveFriendLabel(QString)
{}

void ApplyFriend::slotChangeFriendLabelByTip(QString, ClickLabelState)
{}

void ApplyFriend::slotLabelTextChange(const QString& text)
{}

void ApplyFriend::slotLabelEditFinished()
{}

void ApplyFriend::slotAddFriendLabelByClickTip(const QString& text)
{}

void ApplyFriend::slotApplySure()
{}

void ApplyFriend::slotApplyCancel()
{
	hide();
	deleteLater();
}
