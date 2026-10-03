#pragma once

#include <QFrame>
#include "ui_FriendLabel.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
class FriendLabelClass;
};

QT_END_NAMESPACE

/**
 * 这个是 用户标签
 * 在添加好友界面（ApplyFriend）用到
 */
class FriendLabel : public QFrame
{
	Q_OBJECT

public:
	explicit FriendLabel(QWidget* parent = nullptr);
	~FriendLabel() override;

	void setText(const QString& text);
	[[nodiscard]] int getWidth() const;
	[[nodiscard]] int getHeight() const;
	[[nodiscard]] QString text() const;

public slots:
	void slotClose();

signals:
	void sigClose(QString);

private:
	Ui::FriendLabelClass* ui;

	QString m_text;
	int m_width;
	int m_height;
};
