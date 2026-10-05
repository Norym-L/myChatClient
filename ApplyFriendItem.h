#pragma once

#include <QWidget>
#include "ui_ApplyFriendItem.h"

QT_BEGIN_NAMESPACE
namespace Ui { class ApplyFriendItemClass; };
QT_END_NAMESPACE

class ApplyFriendItem : public QWidget
{
    Q_OBJECT

public:
    explicit ApplyFriendItem(QWidget *parent = nullptr);
    ~ApplyFriendItem() override;

private:
    Ui::ApplyFriendItemClass *ui;
};

