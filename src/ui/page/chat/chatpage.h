#pragma once

#include <QWidget>
#include "ui_chatpage.h"

enum class ChatUiMode
{
    CHAT_MODE,
    SEARCH_MODE,
    CONTACT_MODE,
};

QT_BEGIN_NAMESPACE
namespace Ui
{
class ChatPageClass;
};

QT_END_NAMESPACE

class ChatPage : public QWidget
{
    Q_OBJECT

public:
    explicit ChatPage(QWidget* parent = nullptr);
    ~ChatPage() override;

private:
    /// 是否展示搜索列表
    void showSerachList(bool is_searching = false);
    void initSearchEdit();
    void initChatUserList();
    void initSideBar();
    void addButtonToGroup(StateWidget* widget, const QString& group_name);
    // 刷新侧边栏按钮状态到选中widget
    void freshSideBarStateToSelected(StateWidget* widget, const QString& group_name);

private:

    Ui::ChatPageClass* ui;
    ChatUiMode m_state{ChatUiMode::CHAT_MODE};
    ChatUiMode m_mode{ChatUiMode::CHAT_MODE};
    bool m_beLoadingMoreChatUser{false};
    QMap<QString, QList<StateWidget*>> m_groupedWidgets;
    // TODO 测试用
    void test_addChatUserList() const;


signals:
    void sigSwitchLoginPage();

private slots:
    /// 加载更多联系人
    void slotLoadingMoreChatUserInChatUserList();
    // 侧边栏切换
    void slotSideBarSwitchToChat();
    void slotSideBarSwitchToContact();
    // 搜索框文本变化处理
    void slotSearchEditTextChanged(const QString& text);
};
