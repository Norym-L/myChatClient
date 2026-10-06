#pragma once

#include <QWidget>
#include "ui_ApplyFriendPage.h"
#include "UserData.h"
#include "ApplyFriendItem.h"

QT_BEGIN_NAMESPACE
namespace Ui
{
class ApplyFriendPageClass;
};

QT_END_NAMESPACE

class ApplyFriendPage : public QWidget
{
    Q_OBJECT

public:
    explicit ApplyFriendPage(QWidget* parent = nullptr);
    ~ApplyFriendPage() override;

    // 添加新的申请（不管有没有接受）
    void addNewApply(std::shared_ptr<ApplyInfo> apply);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    // 加载申请列表
    void loadApplyList();

public slots:
    // 点击同意添加好友
    void slotAuthRsp(std::shared_ptr<AuthRsp>);

signals:
    // 搜索
    void sigShowSearch(bool);

private:
    Ui::ApplyFriendPageClass* ui;

    std::map<std::uint64_t, ApplyFriendItem*> m_unauthItems;
};
