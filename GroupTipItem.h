#pragma once

#include <QWidget>
#include "ui_GroupTipItem.h"

QT_BEGIN_NAMESPACE
namespace Ui { class GroupTipItemClass; };
QT_END_NAMESPACE

class GroupTipItem : public QWidget
{
	Q_OBJECT

public:
	explicit GroupTipItem(QWidget* parent = nullptr);
	explicit GroupTipItem(const QString& title, QWidget *parent = nullptr);
	~GroupTipItem() override;

private:
	Ui::GroupTipItemClass *ui;
};

