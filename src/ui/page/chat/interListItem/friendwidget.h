#ifndef CHATCLIENT_FRIENDWIDGET_H
#define CHATCLIENT_FRIENDWIDGET_H

#include <QWidget>

#include "ui/components/ListItemBase.h"


QT_BEGIN_NAMESPACE

namespace Ui
{
class FriendWidget;
}

QT_END_NAMESPACE

class FriendWidget : public ListItemBase
{
    Q_OBJECT

public:
    explicit FriendWidget(QWidget* parent = nullptr);
    ~FriendWidget() override;

    [[nodiscard]] QSize sizeHint() const override;
    void setInfo(const QString& name, const QString& icon, const QString& message);

private:
    Ui::FriendWidget* ui;
    QString m_name;
    QString m_icon;
    QString m_message;
};


#endif //CHATCLIENT_FRIENDWIDGET_H
