#include "ApplyFriend.h"

#include <algorithm>
#include <QBitmap>
#include <QEvent>
#include <QImage>
#include <QKeyEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QFontMetrics>

#include "logger.h"
#include "TCPManager.h"
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
QSize tipLabelSize(const ClickedLabel* label)
{
	return label->sizeHint();
}

/**
 * 已选标签行末尾，留给「新增标签输入框」的横向空间。
 *
 * label_edit 的实际宽度是 ApplyFriend.ui 里的 geometry（108px），
 * 远大于 MIN_APPLY_LABEL_END_LEN(40)。原代码只按 40 判断是否换行，
 * 结果输入框冲出 grid 右边界（实测 344 > 339），看起来就是
 * 「新增标签的输入框显示到外面去了」。取较大者即可同时保留两个约束。
 */
int labelEditNeededWidth(const QLineEdit* edit)
{
	const int width = edit->width();
	return width > MIN_APPLY_LABEL_END_LEN ? width : MIN_APPLY_LABEL_END_LEN;
}

/**
 * 已选标签区（grid）底部留白，与 addLabel() 里换行用的 `+ 6` 同一个值：
 * 一行的占位 = 胶囊高 + 行间距 6，所以最后一行下面也留 6px 才对称。
 */
constexpr int kGridBottomPadding = 6;

/**
 * 空态下 grid 的高度下限，对应 ApplyFriend.ui 里 grid 的设计高度 40px。
 * 没有它的话，加过标签再全删掉，grid 会按「输入框高 26 + 上下各 6」算成 38px，
 * 比初始状态矮 2px —— 视觉上输入区会莫名其妙跳一下。
 */
constexpr int kGridMinHeight = 40;

/**
 * 无边框弹窗的圆角半径，单位：逻辑像素。
 *
 * 必须与 applyfriend.qss 里 QDialog#ApplyFriend 的 border-radius 保持一致：
 * 圆角由「QSS 画底色 + 窗口遮罩切边」两件事配合完成，
 * 两边半径不一致就会出现白边或缺角。改一个必须同时改另一个。
 */
constexpr int kWindowCornerRadius = 8;
} // namespace

ApplyFriend::ApplyFriend(QWidget* parent)
	: QDialog(parent), ui(new Ui::ApplyFriendClass())
{
	ui->setupUi(this);
	setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
	// 圆角靠「窗口遮罩」实现，不用 WA_TranslucentBackground。
	//
	// 窗口级半透明要求 QSS 把不透明底色铺满整窗，只要有一块没画上，
	// 那块就是「透明像素」——在 Windows 上会变成鼠标穿透：
	// 窗口看着还在，点上去却没反应（连它后面的窗口也点不到）。
	// 本页恰恰就有一块没画上：QScrollArea 的 viewport 用调色板的 Window 色
	// 刷底（深色主题下 rgb(30,30,30)），与弹窗底色不一致。
	// 遮罩方案下窗口本身照旧不透明，只把四个角切掉，不涉及穿透。
	// 半径与 QSS 的对应关系见 updateRoundedMask()。
	setObjectName("ApplyFriend");
	setModal(true);

	// QScrollArea::setWidget() 会把内容控件（scrollcontents）的
	// autoFillBackground 置为 true，它会用调色板的 Window 色刷底。
	// 系统深色主题下那个颜色约 rgb(30,30,30)，正好盖住弹窗自己的
	// rgb(34,34,34) 圆角底色 —— 表现为「滚动区 30、下面按钮行 34」的色差。
	// 关掉自动填底，底色统一交给弹窗的 QSS 背景（含圆角）来画。
	ui->scrollArea->widget()->setAutoFillBackground(false);

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

	// 候选标签的起始游标；与 relayoutTipLabels() 里的重置值保持同一个来源
	// （原来这里写死 5，而换行时用 TIP_OFFSET，两个值一变就不一致了）
	m_currentTipPoint = {TIP_OFFSET, TIP_OFFSET};
	m_tipData = {"同学", "家人", "cpp", "java", "golang", "test", "js", "type", "ts", "namespace", "do somthing"};

	initTipLabel();
	connect(ui->more_label, &ClickedOnceLabel::released, this, &ApplyFriend::slotShwoMoreLabel);
	connect(ui->label_edit, &TruncateEdit::returnPressed, this, &ApplyFriend::slotLabelEnter);
	connect(ui->label_edit, &TruncateEdit::textChanged, this, &ApplyFriend::slotLabelTextChange);
	connect(ui->label_edit, &TruncateEdit::editingFinished, this, &ApplyFriend::slotLabelEditFinished);
	connect(ui->tip_label, &ClickedOnceLabel::clicked, this, &ApplyFriend::slotAddFriendLabelByClickTip);

	// 关掉「自动默认按钮」，否则回车会把整个申请页关掉。
	//
	// 原因：QDialog 里 QPushButton 的 autoDefault 默认为 true，且第一个
	// autoDefault 按钮会自动成为 default button。而 QLineEdit 处理回车时
	// 发完 returnPressed 会对事件调 ignore()，事件继续冒泡到 QDialog，
	// QDialog 就把它当成「确认默认按钮」→ click() → ClickedButton::released
	// → slotApplySure() → hide() + deleteLater()。
	// 实测：在 label_edit 输入按回车，标签确实加上了，
	//       但紧接着弹窗 isVisible() 由 1 变成 0，页面就没了。
	// 标签输入框里回车只应表示「添加这个标签」，不能连带提交/关闭页面。
	ui->sure_button->setAutoDefault(false);
	ui->sure_button->setDefault(false);
	ui->cancel_button->setAutoDefault(false);
	ui->cancel_button->setDefault(false);

	ui->scrollArea->horizontalScrollBar()->setHidden(true);
	ui->scrollArea->verticalScrollBar()->setHidden(true);
	ui->scrollArea->installEventFilter(this);
	ui->sure_button->setState("normal", "hover", "released");
	ui->cancel_button->setState("normal", "hover", "released");

	connect(ui->sure_button, &ClickedButton::released, this, &ApplyFriend::slotApplySure);
	connect(ui->cancel_button, &ClickedButton::released, this, &ApplyFriend::slotApplyCancel);

	// 先把遮罩按当前尺寸切好，避免首次 show 时出现「方角一闪」。
	// 真正的最终尺寸要到 show 之后才定下来，那时由 resizeEvent 重建。
	updateRoundedMask();
}

ApplyFriend::~ApplyFriend()
{
	delete ui;
}

void ApplyFriend::updateRoundedMask()
{
	constexpr int kSuperSample = 4; // 4 倍超采样：1bit 遮罩本身没有抗锯齿，靠缩放取阈值柔化

	const QSize logical = size();
	if (logical.isEmpty())
	{
		return;
	}
	const QSize canvas_size = logical * kSuperSample;

	QImage canvas(canvas_size, QImage::Format_ARGB32_Premultiplied);
	canvas.fill(Qt::transparent);
	{
		QPainter painter(&canvas);
		painter.setRenderHint(QPainter::Antialiasing, true);
		painter.setPen(Qt::NoPen);
		painter.setBrush(Qt::black);
		// 向外扩 0.5 个采样像素：保证四条边最外侧的像素被完全覆盖。
		// 否则缩放取遮罩后最外一圈可能被判成「不足一半覆盖率」而被切掉，
		// 窗口看上去像被削了 1px 边。
		painter.drawRoundedRect(
			QRectF(-0.5, -0.5, canvas_size.width() + 1.0, canvas_size.height() + 1.0),
			kWindowCornerRadius * kSuperSample, kWindowCornerRadius * kSuperSample);
	}

	// 缩回逻辑尺寸再取遮罩：边界落在约 50% 覆盖率处，比直接画 1bit 锯齿轻。
	setMask(QPixmap::fromImage(
	            canvas.scaled(logical, Qt::IgnoreAspectRatio, Qt::SmoothTransformation))
	            .mask());
}

void ApplyFriend::resizeEvent(QResizeEvent* event)
{
	QDialog::resizeEvent(event);
	// 遮罩是按尺寸切的，尺寸一变就过期；不重建的话窗口被改大后
	// 圆角处会残留旧遮罩的形状（表现为四角被切掉一块）。
	updateRoundedMask();
}

void ApplyFriend::relayoutTipLabels()
{
	// 把候选区里【已存在】的标签按 m_addLabelKeys 的顺序重排一遍，
	// 并按内容撑高 label_list。不新建、不删除任何控件。
	// 抽出来给两处共用：
	//   - slotShwoMoreLabel()      展开「更多」后重排
	//   - slotRemoveFriendLabel()  删掉临时标签后让其余标签补位
	m_currentTipPoint = {TIP_OFFSET, TIP_OFFSET};

	int last_row_y = m_currentTipPoint.y();
	int last_row_height = 0;
	for (const auto& key : m_addLabelKeys)
	{
		auto* label = m_addLabels.value(key, nullptr);
		if (label == nullptr)
		{
			continue;
		}

		// 用控件实际占位（含 QSS 的 padding / border），不能用文字宽度，
		// 否则标签会互相压住，见 tipLabelSize 注释
		const QSize label_size = tipLabelSize(label);
		const int text_width = label_size.width();
		const int text_height = label_size.height();

		if (m_currentTipPoint.x() + text_width + TIP_OFFSET > ui->label_list->width())
		{
			m_currentTipPoint.setX(TIP_OFFSET);
			m_currentTipPoint.setY(m_currentTipPoint.y() + text_height + TIP_OFFSET);
		}

		label->move(m_currentTipPoint);
		label->show();

		m_currentTipPoint.setX(m_currentTipPoint.x() + text_width + TIP_OFFSET);
		last_row_y = m_currentTipPoint.y();
		last_row_height = text_height;
	}

	if (last_row_height > 0)
	{
		// 高度只能按纵坐标算，且每次都重算（可增可缩）
		ui->label_list->setFixedHeight(last_row_y + last_row_height + TIP_OFFSET);
	}
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

void ApplyFriend::setSerachInfo(const std::shared_ptr<SearchInfo>& si){
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

	if (!m_friendLabels.isEmpty() && m_labelPoint.x() + labelEditNeededWidth(ui->label_edit) > ui->grid->width())
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
	// grid 里的 FriendLabel 是用 move() 手工摆的，grid 没有 layout，
	// 所以它的高度只能自己算：取所有子控件里的最低边界 + 底部留白。
	//
	// 只做「重新算一遍并赋值」，**不做任何增量累加**。之前那版就是栽在
	// 累加和「只增不减」上：
	//
	//   1) 用 scrollcontents->setFixedHeight(当前高 + 差值) 做增量累加，
	//      只会单调变大。实测：加 9 个标签 520px → 859px，再把标签全删掉，
	//      仍然是 859px，表单底部留出一大片永远回不去的空白。
	//   2) friend_group_widget->setMinimumHeight() 外面套了「比当前值小才写」，
	//      同样是只增不减，删标签后容器继续撑着一块空白。
	//   3) 同一份滚动高度上其实有三个累加器（本函数、
	//      createAnNewTipLabel、slotShwoMoreLabel 各自加减一次），
	//      最终值已经无法从内容推导，调用顺序一变结果就变。
	//
	// 现在只负责 grid 自己：外层一律不手工动。friend_group_widget 和
	// scrollcontents 都有 layout，而 scrollArea 的 widgetResizable 是打开的
	// （见 ApplyFriend.ui），grid 的高度一变，布局会自己把需要的高度往上传递，
	// 内容超高时自动出现滚动条，删掉标签也能自动缩回去。
	int bottom = ui->label_edit->y() + ui->label_edit->height();
	for (const auto& label : m_friendLabels)
	{
		bottom = std::max(bottom, label->y() + label->height());
	}

	// setFixedHeight 同时写 min 和 max，下次重算即可增可减；
	// .ui 里 grid 的 40px 上限也因此被解除。
	ui->grid->setFixedHeight(std::max(bottom + kGridBottomPadding, kGridMinHeight));
}

void ApplyFriend::addLabel(const QString& name)
{
	ui->label_edit->clear();
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
	if (m_labelPoint.x() + labelEditNeededWidth(ui->label_edit) > ui->grid->width())
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
	m_currentAddedLabels.push_back(text);

	connect(label, &ClickedLabel::releasedWithNameAndState, this, &ApplyFriend::slotChangeFriendLabelByTip);

	// 同 initTipLabel：按控件实际占位排布，否则候选标签互相重叠
	const QSize label_size = tipLabelSize(label);
	const auto text_width = label_size.width();
	const auto text_height = label_size.height();

	if (m_currentTipPoint.x() + text_width + TIP_OFFSET > ui->label_list->width())
	{
		m_currentTipPoint.setX(TIP_OFFSET);
		m_currentTipPoint.setY(m_currentTipPoint.y() + text_height + TIP_OFFSET);
	}

	auto next_point = m_currentTipPoint;
	addTipLabel(label, m_currentTipPoint, next_point, text_width, text_height);
	m_currentTipPoint = next_point;

	// 只设候选区自己的高度；滚动内容交给 scrollArea 的 widgetResizable 推导。
	// 原先这里还做了一次 scrollcontents->setFixedHeight(当前高 + 差值) 的累加，
	// 与 adjustGridHeight() 各自累加同一份高度，删标签后缩不回去。
	ui->label_list->setFixedHeight(next_point.y() + text_height + TIP_OFFSET);
	label->setCurrentState(ClickLabelState::SELECT);
}

void ApplyFriend::slotShwoMoreLabel()
{
	ui->more_label_widget->hide();
	ui->label_list->setFixedWidth(325);

	// 补齐 m_tipData 里还没有胶囊的标签。initTipLabel() 有「最多 2 行」的
	// 截断，所以被截掉的那些要在这里补出来。先只创建、不摆位置，
	// 位置统一交给下面的 relayoutTipLabels()。
	for (const auto& data : m_tipData)
	{
		if (m_addLabels.contains(data))
		{
			continue;
		}

		auto* lb = new ClickedLabel(ui->label_list);
		lb->setState("normal", "hover", "pressed", "selected_normal",
		             "selected_hover", "selected_pressed");
		lb->setObjectName("tipslb");
		lb->setText(data);
		connect(lb, &ClickedLabel::releasedWithNameAndState, this, &ApplyFriend::slotChangeFriendLabelByTip);

		m_addLabels.insert(data, lb);
		m_addLabelKeys.push_back(data);
	}

	// 与「删掉临时标签后补位」共用同一套重排逻辑
	relayoutTipLabels();
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
	// 加完在addLabel里清空，确保完成输入也能清空

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

	// 临时标签（用户本次自己敲出来的，建仓时登记在 m_currentAddedLabels 里）
	// 一旦取消就要整条删除：候选区的胶囊、候选数据、登记记录一起清掉，
	// 否则会留下一个再也点不亮的死标签；其余候选标签重排补位。
	//
	// 注意顺序：先用 removeAll 的返回值判断、并把 m_addLabels 里对应的
	// 控件 deleteLater 掉再 erase，最后才重排——不能像原来那样先重排
	// 再去 find(name)，那样拿到的可能是已经删掉的控件。
	if (m_currentAddedLabels.removeAll(name) > 0)
	{
		m_tipData.removeAll(name);
		if (const auto it = m_addLabels.find(name); it != m_addLabels.end())
		{
			it.value()->deleteLater();
			m_addLabels.erase(it);
		}
		m_addLabelKeys.removeAll(name);
		relayoutTipLabels();
	}
	else if (const auto it = m_addLabels.find(name); it != m_addLabels.end())
	{
		// 预置标签只取消选中，候选区里的胶囊保留（已经有的标签不要删除）
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

	QJsonObject json_object;
	json_object["uid"] = QString::number(UserManager::GetInstance().getUid());
	json_object["verify_info"] = ui->verify_info_edit->text().isEmpty() ? ui->verify_info_edit->placeholderText() : ui->verify_info_edit->text();
	json_object["remark"] = ui->back_name_edit->text().isEmpty() ? ui->back_name_edit->placeholderText() : ui->back_name_edit->text();
	json_object["to_uid"] = QString::number(m_searchInfo->uid);

	const QJsonDocument document(json_object);
	const QByteArray json_data = document.toJson(QJsonDocument::Compact);

	emit TCPManager::GetInstance().sigSendData(ReqId::ID_ADD_FRIEDN_REQ, std::move(json_data));

	hide();
	deleteLater();
}

void ApplyFriend::slotApplyCancel()
{
	LOG_DEBUG() << "[friend apply page] slot apply cancel";
	hide();
	deleteLater();
}
