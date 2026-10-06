#pragma once
#include <QListWidget>

class ApplyFriendList : public QListWidget
{
    Q_OBJECT

public:
    explicit ApplyFriendList(QWidget* parent = nullptr);

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private slots:
signals:
    void sigShowSearch(bool);
};
