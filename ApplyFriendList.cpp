#include "ApplyFriendList.h"

#include <QWheelEvent>
#include <QScrollBar>

ApplyFriendList::ApplyFriendList(QWidget* parent)
    : QListWidget(parent)
{
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    viewport()->installEventFilter(this);
}

bool ApplyFriendList::eventFilter(QObject* object, QEvent* event)
{
    // 检查事件是否是鼠标悬浮进入或离开
    if (object == this->viewport())
    {
        if (event->type() == QEvent::Enter)
        {
            // 鼠标悬浮，显示滚动条
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }
        else if (event->type() == QEvent::Leave)
        {
            // 鼠标离开，隐藏滚动条
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }

    if (object == this->viewport())
    {
        if (event->type() == QEvent::MouseButtonPress)
        {
            emit sigShowSearch(false);
        }
    }

    // 检查事件是否是鼠标滚轮事件
    if (object == this->viewport() && event->type() == QEvent::Wheel)
    {
        const QWheelEvent* wheelEvent = dynamic_cast<QWheelEvent*>(event);
        const int numDegrees = wheelEvent->angleDelta().y() / 8;
        const int numSteps = numDegrees / 15; // 计算滚动步数

        // 设置滚动幅度
        this->verticalScrollBar()->setValue(this->verticalScrollBar()->value() - numSteps);

        return true; // 停止事件传递
    }

    return QListWidget::eventFilter(object, event);
}
