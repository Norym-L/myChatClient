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
    void initSideBar() const;

    Ui::ChatPageClass* ui;
    ChatUiMode m_state{ChatUiMode::CHAT_MODE};
    ChatUiMode m_mode{ChatUiMode::CHAT_MODE};
    bool m_beLoadingMoreChatUser{false};

    // TODO 测试用
    void test_addChatUserList() const;


signals:
    void sigSwitchLoginPage();

private slots:
    /// 加载更多联系人
    void slotLoadingMoreChatUserInChatUserList();
};
