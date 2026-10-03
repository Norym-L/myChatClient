#pragma once

#include <QDialog>
#include "ui_ApplyFriend.h"

#include "clicked_label.h"
#include "FriendLabel.h"
#include "UserData.h"

constexpr int MIN_APPLY_LABEL_END_LEN = 40; // 好友标签输入框（已添加的标签）最小长度
consteval QString ADD_TIP_PERFIX = "添加标签："; // 输入标签时，提示框的前缀
consteval int TIP_OFFSET = 5; // 标签偏移量

QT_BEGIN_NAMESPACE
namespace Ui
{
class ApplyFriendClass;
};

QT_END_NAMESPACE

/**
 * 添加好友页面 输入自我介绍、备注、标签等
 * 由 findresultdialog.h 调用
 */
class ApplyFriend : public QDialog
{
	Q_OBJECT

public:
	explicit ApplyFriend(QWidget* parent = nullptr);
	~ApplyFriend() override;

	void initTipLabel();
	// 添加用户标签（FriendLabel）
	void addTipLabel(ClickedLabel* label, QPoint current_point, QPoint& next_point, int text_width, int text_height);
	void setSerachInfo(const std::shared_ptr<SearchInfo>& si);

protected:
	bool eventFilter(QObject*, QEvent*) override;

private:
	// 重置标签（位置）
	void resetLabel();
	// 添加标签
	void addLabel(const QString& name);

private slots:
	// 展示更多好友标签
	void slotShwoMoreLabel();
	// 按下回车展示输入的标签
	void slotLabelEnter();
	// 点击关闭移除好友标签
	void slotRemoveFriendLabel(QString);
	// 点击tip实现增加减少好友的标签
	void slotChangeFriendLabelByTip(QString, ClickLabelState);
	// 输入框文本变化展示不同的标签
	void slotLabelTextChange(const QString& text);
	// 输入完成
	void slotLabelEditFinished();
	// 点击提示框增加好友标签
	void slotAddFriendLabelByClickTip(const QString& text);
	// 确认按钮
	void slotApplySure();
	// 取消按钮
	void slotApplyCancel();

private:
	Ui::ApplyFriendClass* ui;

	// 已经有的标签
	QMap<QString, ClickedLabel*> m_addLabels;
	QVector<QString> m_addLabelKeys;
	QPoint m_labelPoint;
	// 输入框显示的标签
	QMap<QString, FriendLabel*> m_friendLabels;
	QVector<QString> m_friendLabelKeys;
	QVector<QString> m_tipdata;
	QPoint m_currentTipPoint;
	std::shared_ptr<SearchInfo> m_searchInfo;
};
