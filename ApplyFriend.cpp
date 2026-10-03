#include "ApplyFriend.h"

#include <QScrollBar>
#include <QFontMetrics>

#include "logger.h"
#include "UserManager.h"

namespace
{
/**
 * 候选标签（ClickedLabel）的实际占用尺寸。
 *
 * 必须问控件自己（sizeHint 已包含 QSS 给的 padding 与 border），
 * 不能用 fontMetrics.horizontalAdvance(text) —— 那只算文字宽度，
 * 比控件真实宽度少一截（实测 14px 字号 + padding 3px/10px 时少 36px），
 * 后果就是候选标签一个压一个。
 */
QSize tipLabelSize(ClickedLabel* label)
{
	return label->sizeHint();
}
} // namespace

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
	// 与 FriendLabel 胶囊同高：FriendLabel::setText 用同一公式
	// （fontMetrics.height() + 14）。原值 20px 会让输入框比已选标签矮一截，
	// 同一行里基线错位。
	ui->label_edit->setFixedHeight(QFontMetrics(ui->label_edit->font()).height() + 14);
	ui->label_edit->setMaxLength(10);
	ui->input_tip->hide();

	m_currentTipPoint = {5, 5};
	m_tipData = {"同学", "家人", "cpp", "java", "golang", "test", "js", "type", "ts", "namespace", "do somthing"};

	initTipLabel();
	connect(ui->more_label, &ClickedOnceLabel::released, this, &ApplyFriend::slotShwoMoreLabel);
	connect(ui->label_edit, &TruncateEdit::returnPressed, this, &ApplyFriend::slotLabelEnter);
	connect(ui->label_edit, &TruncateEdit::textChanged, this, &ApplyFriend::slotLabelTextChange);
	connect(ui->label_edit, &TruncateEdit::editingFinished, this, &ApplyFriend::slotLabelEditFinished);
	connect(ui->tip_label, &ClickedOnceLabel::clicked, this, &ApplyFriend::slotAddFriendLabelByClickTip);

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
{
	int lines = 1;
	for (const auto& data : m_tipData)
	{
		auto* label = new ClickedLabel(ui->label_list);
		label->setState("normal", "hover", "pressed", "selected_normal", "selected_hover", "selected_pressed");
		label->setObjectName("tipslb");
		label->setText(data);
		connect(label, &ClickedLabel::releasedWithNameAndState, this, &ApplyFriend::slotChangeFriendLabelByTip);

		// 按控件实际占位排布（含 QSS 的 padding / border），
		// 用文字宽度会导致候选标签互相重叠，见 tipLabelSize 注释
		const QSize label_size = tipLabelSize(label);
		const int text_width = label_size.width();
		const int text_height = label_size.height();

		if (m_currentTipPoint.x() + text_width + TIP_OFFSET > ui->label_list->width())
		{
			lines++;
			if (lines > 2)
			{
				delete label;
				return;
			}

			m_currentTipPoint.setX(TIP_OFFSET);
			m_currentTipPoint.setY(m_currentTipPoint.y() + text_height + TIP_OFFSET);
		}

		auto next_point = m_currentTipPoint;
		addTipLabel(label, m_currentTipPoint, next_point, text_width, text_height);
		m_currentTipPoint = next_point;
	}
}

void ApplyFriend::addTipLabel(ClickedLabel* label, const QPoint current_point, QPoint& next_point, const int text_width,
                              const int text_height)
{
	label->move(current_point);
	label->show();
	m_addLabels.insert(label->text(), label);
	m_addLabelKeys.push_back(label->text());
	next_point.setX(label->pos().x() + text_width + TIP_OFFSET);
	next_point.setY(label->pos().y());
}

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

void ApplyFriend::resetLabels()
{
	const auto max_width = ui->grid->width();
	auto label_heigth = 0;
	for (auto it = m_friendLabels.begin(); it != m_friendLabels.end(); ++it)
	{
		if (m_labelPoint.x() + it.value()->width() > max_width)
		{
			m_labelPoint.setX(2);
			m_labelPoint.setY(m_labelPoint.y() + it.value()->height() + 6);
		}

		it.value()->move(m_labelPoint);
		it.value()->show();

		m_labelPoint.setX(m_labelPoint.x() + it.value()->width() + 2);
		label_heigth = it.value()->height();
	}

	if (!m_friendLabels.isEmpty() && m_labelPoint.x() + ui->label_edit->width() > ui->grid->width())
	{
		ui->label_edit->move(2, m_labelPoint.y() + label_heigth + 6);
	}
	else
	{
		ui->label_edit->move(m_labelPoint);
	}

	adjustGridHeight();
}

void ApplyFriend::adjustGridHeight()
{
	// grid 在 ApplyFriend.ui 里被固定成 40px 高，而标签胶囊的高度由
	// FriendLabel 按字号动态算出（14px 字体下约 28px，真机字体约 33px）。
	// 标签换行后第二行会落到 40px 之外、被子控件裁切，所以这里按实际
	// 子控件的最低边界重新撑高 grid，使其随行数增长
	// （与 slotShwoMoreLabel() 里撑高 scrollcontents 的做法一致）。
	int bottom = ui->label_edit->y() + ui->label_edit->height();
	for (const auto& label : m_friendLabels)
	{
		if (label->y() + label->height() > bottom)
		{
			bottom = label->y() + label->height();
		}
	}
	ui->grid->setFixedHeight(bottom + 6); // 6 = 底部留白
}

void ApplyFriend::addLabel(const QString& name)
{
	if (m_friendLabels.find(name) != m_friendLabels.end())
	{
		return;
	}
	const auto tmp_label = new FriendLabel(ui->grid);
	tmp_label->setText(name);
	tmp_label->setObjectName("friendLabel");

	const auto max_width = ui->grid->width();
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
	// 必须按输入框的实际宽度判断换行：label_edit 宽 108px（ApplyFriend.ui 里的
	// geometry），而 MIN_APPLY_LABEL_END_LEN 只有 40，用它判断会让输入框
	// 超出 grid 右边界（实测 344 > 339），看起来就是「输入框跑到了外面」。
	if (m_labelPoint.x() + ui->label_edit->width() > ui->grid->width())
	{
		ui->label_edit->move(2, m_labelPoint.y() + tmp_label->height() + 2);
	}
	else
	{
		ui->label_edit->move(m_labelPoint);
	}

	adjustGridHeight();
}

void ApplyFriend::createAnNewTipLabel(const QString& text)
{
	auto* label = new ClickedLabel(ui->label_list);
	label->setState("normal", "hover", "pressed", "selected_normal", "selected_hover", "selected_pressed");
	label->setObjectName("tipsLabel");
	label->setText(text);

	connect(label, &ClickedLabel::releasedWithNameAndState, this, &ApplyFriend::slotChangeFriendLabelByTip);

	const QFontMetrics font_metrics(label->font());
	const auto text_width = font_metrics.horizontalAdvance(label->text());
	const auto text_height = font_metrics.height();

	if (m_currentTipPoint.x() + text_width + TIP_OFFSET > ui->label_list->width())
	{
		m_currentTipPoint.setX(TIP_OFFSET);
		m_currentTipPoint.setY(m_currentTipPoint.y() + text_height + TIP_OFFSET);
	}

	auto next_point = m_currentTipPoint;
	addTipLabel(label, m_currentTipPoint, next_point, text_width, text_height);
	m_currentTipPoint = next_point;

	const auto diff_height = next_point.y() + text_height + TIP_OFFSET - ui->label_list->height();
	ui->label_list->setFixedHeight(next_point.y() + text_height + TIP_OFFSET);
	label->setCurrentState(ClickLabelState::SELECT);
	ui->scrollcontents->setFixedHeight(ui->scrollcontents->height() + diff_height);
}

void ApplyFriend::slotShwoMoreLabel()
{
	ui->more_label_widget->hide();
	ui->label_list->setFixedWidth(325);

	m_currentTipPoint = {5, 5};
	auto next_point = m_currentTipPoint;
	int text_width{0}, text_height{0};
	for (auto& key : m_addLabelKeys) // 重排现有的
	{
		const auto added_label = m_addLabels[key];
		QFontMetrics font_metrics(added_label->font());
		text_width = font_metrics.horizontalAdvance(added_label->text());
		text_height = font_metrics.height();

		if (m_currentTipPoint.x() + text_width + TIP_OFFSET > ui->label_list->width())
		{
			m_currentTipPoint.setX(TIP_OFFSET);
			m_currentTipPoint.setY(m_currentTipPoint.y() + text_height + 15);
		}

		added_label->move(m_currentTipPoint);

		next_point.setX(added_label->pos().x() + text_width + 15);
		next_point.setY(m_currentTipPoint.y());

		m_currentTipPoint = next_point;
	}
	for (const auto& data : m_tipData) //添加未添加的
	{
		const auto iter = m_addLabels.find(data);
		if (iter != m_addLabels.end())
		{
			continue;
		}

		auto* lb = new ClickedLabel(ui->label_list);
		lb->setState("normal", "hover", "pressed", "selected_normal",
		             "selected_hover", "selected_pressed");
		lb->setObjectName("tipslb");
		lb->setText(data);
		connect(lb, &ClickedLabel::releasedWithNameAndState, this, &ApplyFriend::slotChangeFriendLabelByTip);

		QFontMetrics fontMetrics(lb->font()); // 获取QLabel控件的字体信息
		const int textWidth = fontMetrics.horizontalAdvance(lb->text()); // 获取文本的宽度
		const int textHeight = fontMetrics.height(); // 获取文本的高度

		if (m_currentTipPoint.x() + textWidth + TIP_OFFSET > ui->label_list->width())
		{
			m_currentTipPoint.setX(TIP_OFFSET);
			m_currentTipPoint.setY(m_currentTipPoint.y() + textHeight + 15);
		}

		next_point = m_currentTipPoint;

		addTipLabel(lb, m_currentTipPoint, next_point, textWidth, textHeight);

		m_currentTipPoint = next_point;
	}

	const auto diff_height = next_point.y() + text_height + TIP_OFFSET - ui->label_list->height();
	ui->label_list->setFixedHeight(next_point.x() + text_height + TIP_OFFSET);
	ui->scrollcontents->setFixedHeight(ui->scrollcontents->height() + diff_height);
}

void ApplyFriend::slotLabelEnter()
{
	const auto text = ui->label_edit->text();
	if (text.isEmpty())
	{
		return;
	}

	addLabel(text);
	ui->input_tip->hide();

	if (!m_tipData.contains(text))
	{
		m_tipData.push_back(text);
	}

	// 判断展示的标签是否有text
	if (m_addLabels.contains(text))
	{
		m_addLabels[text]->setCurrentState(ClickLabelState::SELECT);
	}
	else
	{
		createAnNewTipLabel(text);
	}
}

void ApplyFriend::slotRemoveFriendLabel(const QString& name)
{
	m_labelPoint.setX(2);
	m_labelPoint.setY(6);

	if (const auto it = m_friendLabels.find(name); it != m_friendLabels.end())
	{
		it.value()->deleteLater();
		m_friendLabels.erase(it);
	}
	if (const auto it = m_friendLabelKeys.removeAll(name); it > 0)
	{
		if (it > 1)
		{
			LOG_WARN() << "[friend apply page][tips] removed tips > 2! has same tips! tip: " << name;
		}
		LOG_DEBUG() << "[friend apply page][tips] remove tip: " << name;
	}

	resetLabels();

	if (const auto it = m_addLabels.find(name); it != m_addLabels.end()) // 已经有的标签不要删除，只删掉新建的标签
	{
		it.value()->resetState();
	}
}

void ApplyFriend::slotChangeFriendLabelByTip(const QString& label_text, const ClickLabelState state)
{
	if (const auto it = m_addLabels.find(label_text); it != m_addLabels.end())
	{
		if (state == ClickLabelState::SELECT)
		{
			// TODO: 添加逻辑
			addLabel(label_text);
		}
		else
		{
			// TODO: 删除逻辑
			slotRemoveFriendLabel(label_text);
		}
	}
}

void ApplyFriend::slotLabelTextChange(const QString& text) const
{
	if (text.isEmpty())
	{
		ui->tip_label->setText("");
		ui->input_tip->hide();
		return;
	}

	if (!m_tipData.contains(text))
	{
		ui->tip_label->setText(ADD_TIP_PERFIX + text);
		ui->input_tip->show();
		return;
	}

	ui->tip_label->setText(text);
	ui->input_tip->show();
}

void ApplyFriend::slotLabelEditFinished() const
{
	ui->input_tip->hide();
}

void ApplyFriend::slotAddFriendLabelByClickTip(const QString& text)
{
	LOG_DEBUG() << "[apply friend page] a new tip is: " << text;
	ui->tip_label->setEnable(true);

	QString add_text = text;
	if (const auto index = add_text.indexOf(ADD_TIP_PERFIX); index != -1) // 去掉前缀
	{
		add_text = add_text.mid(index + ADD_TIP_PERFIX.length());
	}
	addLabel(add_text);

	if (!m_tipData.contains(add_text)) // 设置选中
	{
		m_tipData.push_back(add_text);
	}

	if (const auto it = m_addLabels.find(add_text); it != m_addLabels.end()) // 如果标签存在
	{
		it.value()->setCurrentState(ClickLabelState::SELECT);
	}
	else
	{
		createAnNewTipLabel(add_text);
	}
}

void ApplyFriend::slotApplySure()
{
	LOG_DEBUG() << "[friend apply page] slot apply sure";
	hide();
	deleteLater();
}

void ApplyFriend::slotApplyCancel()
{
	LOG_DEBUG() << "[friend apply page] slot apply cancel";
	hide();
	deleteLater();
}
