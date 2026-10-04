#pragma once

#include "ui_ContactUserItem.h"

#include "ListItemBase.h"
#include "global.h"
#include "UserData.h"

QT_BEGIN_NAMESPACE
namespace Ui { class ContactUserItemClass; };
QT_END_NAMESPACE

class ContactUserItem : public ListItemBase
{
	Q_OBJECT

public:
	explicit ContactUserItem(QWidget *parent = nullptr);
	~ContactUserItem() override;

	[[nodiscard]] QSize sizeHint() const override;
	void setInfo(const std::shared_ptr<AuthInfo>& auth_info);
	void setInfo(const std::shared_ptr<AuthRsp>& auth_rsp);
	void setInfo(int uid, const QString& name, const QString& icon, const QString& nick = {});
	void showRedPoint(bool show = true) const;

private:
	Ui::ContactUserItemClass *ui;
	std::shared_ptr<UserInfo> m_info;
};

