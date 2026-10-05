#pragma once

#include "ListItemBase.h"

#include "ui_GroupTipItem.h"

QT_BEGIN_NAMESPACE
namespace Ui { class GroupTipItemClass; };
QT_END_NAMESPACE

class GroupTipItem : public ListItemBase
{
	Q_OBJECT

public:
	explicit GroupTipItem(QWidget* parent = nullptr);
	explicit GroupTipItem(const QString& title, QWidget *parent = nullptr);
	~GroupTipItem() override;

	[[nodiscard]] QSize sizeHint() const override;
	void setGroupTitle(const QString& title);

private:
	Ui::GroupTipItemClass *ui;
	QString m_groupTitle{};
};

