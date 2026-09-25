#include "ChatUserList.h"

#include <QEvent>
#include <QWheelEvent>
#include <QScrollBar>

#include "core/logger.h"

ChatUserList::ChatUserList(QWidget *parent)
    : QListWidget(parent)
{
    (void)parent;
    this->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 安装事件过滤器
    this->viewport()->installEventFilter(this);
}

bool ChatUserList::eventFilter(QObject *object, QEvent *event)
{
    // 检查鼠标悬浮进入或离开
    if (object == this->viewport())
    {
        if (event->type() == QEvent::Enter) // 是否显示进度条
        {
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
        }
        else if (event->type() == QEvent::Leave)
        {
            this->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
    }

    // 滚轮滚动事件
    if (object == this->viewport() && event->type() == QEvent::Wheel)
    {
        const auto *wheel_event = dynamic_cast<QWheelEvent*>(event);
        const int num_degress = wheel_event->angleDelta().y() / 8;
        const int num_steps = num_degress / 15; // 计算滚动步数
        // 设置幅度
        this->verticalScrollBar()->setValue(this->verticalScrollBar()->value() - num_steps);
        // 检查是否滚动到底部
        const auto* scroll_bar = this->verticalScrollBar();

        if (scroll_bar->maximum() - scroll_bar->value() <= 0)
        {
            // 滚动到底部，需要加载新的联系人
            LOG_DEBUG() << "[ui][chat page] load more chat user";
            emit sigLoadingChatUser();
        }

        return true;
    }

    return QListWidget::eventFilter(object, event);
}
