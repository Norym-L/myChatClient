#ifndef CHATCLIENT_ADDUSERITEM_H
#define CHATCLIENT_ADDUSERITEM_H

#include "ListItemBase.h"


QT_BEGIN_NAMESPACE

namespace Ui
{
class AddUserItem;
}

QT_END_NAMESPACE

class AddUserItem : public ListItemBase
{
    Q_OBJECT

public:
    explicit AddUserItem(QWidget* parent = nullptr);
    ~AddUserItem() override;

    [[nodiscard]] QSize sizeHint() const override;

private:
    Ui::AddUserItem* ui;
};


#endif //CHATCLIENT_ADDUSERITEM_H
