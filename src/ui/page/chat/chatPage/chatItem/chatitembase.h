#ifndef CHATCLIENT_CHATITEMBASE_H
#define CHATCLIENT_CHATITEMBASE_H

#include <QWidget>
#include "bubbleframe.h"

/// 聊天角色
enum class ChatRole;

QT_BEGIN_NAMESPACE

namespace Ui
{
class ChatItemBase;
}

QT_END_NAMESPACE

class ChatItemBase : public QWidget
{
    Q_OBJECT

public:
    explicit ChatItemBase(ChatRole role, QWidget* parent = nullptr);
    ~ChatItemBase() override;

    void setUserName(const QString& name) const;
    void setUserIcon(const QPixmap& icon) const;
    void setWidget(QWidget* widget);

private:
    ChatRole m_role;
    QWidget* m_widget;

    Ui::ChatItemBase* ui;
};


#endif //CHATCLIENT_CHATITEMBASE_H
