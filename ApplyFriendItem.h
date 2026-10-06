#pragma once

#include "ListItemBase.h"
#include "ui_ApplyFriendItem.h"

#include "UserData.h"

QT_BEGIN_NAMESPACE
namespace Ui { class ApplyFriendItemClass; };
QT_END_NAMESPACE

class ApplyFriendItem : public ListItemBase
{
    Q_OBJECT

public:
    explicit ApplyFriendItem(QWidget *parent = nullptr);
    ~ApplyFriendItem() override;

    void setInfo(std::shared_ptr<ApplyInfo> apply_info);
    void setInfo(std::shared_ptr<AddFriendApply> apply);
    [[nodiscard]] std::shared_ptr<ApplyInfo> getInfo() const;
    void setTipText(const QString& text) const;
    void showAddButton(bool show);
    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] std::uint64_t getUid() const;

    signals:
    /// 点击添加按钮发送信号
    void sigAuthFriend(std::shared_ptr<ApplyInfo> apply_info);

private:
    Ui::ApplyFriendItemClass *ui;

    std::shared_ptr<ApplyInfo> m_apply_info;
    bool m_beAdded{false};
};

