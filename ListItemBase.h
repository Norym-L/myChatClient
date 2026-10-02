#ifndef CHATCLIENT_LISTITEMBASE_H
#define CHATCLIENT_LISTITEMBASE_H

enum class ListItemType
{
    CHAT_USER,    // 聊天用户
    CONTACT_USER, // 联系人
    SEARCH_USER,  // 搜索到的用户
    ADD_USER_TIP, // 添加用户提示
    INVALID,      // 不可点击条目
    GROUP_TIP,    // 分组提示条目
};

#include <QWidget>
#include <QPainter>
#include <QStyleOption>

class ListItemBase : public QWidget
{
    Q_OBJECT

public:
    explicit ListItemBase(QWidget *parent = nullptr) : QWidget(parent) {}
    void setItemType(const ListItemType item_type) { m_type = item_type; }
    [[nodiscard]] ListItemType getType() const { return m_type; }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QStyleOption option;
        option.initFrom(this);
        QPainter painter(this);
        style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, this);
    }

private:
    ListItemType m_type{};
};


#endif //CHATCLIENT_LISTITEMBASE_H
