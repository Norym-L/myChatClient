#pragma once

#include <QFrame>
#include "ui_FriendLabel.h"

QT_BEGIN_NAMESPACE
namespace Ui { class FriendLabelClass; };
QT_END_NAMESPACE

class FriendLabel : public QFrame
{
	Q_OBJECT

public:
	explicit FriendLabel(QWidget *parent = nullptr);
	~FriendLabel() override;

private:
	Ui::FriendLabelClass *ui;
};

