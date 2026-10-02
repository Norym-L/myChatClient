#ifndef CHATCLIENT_BUBBLEFRAME_H
#define CHATCLIENT_BUBBLEFRAME_H

#include <QFrame>

/// 聊天角色
enum class ChatRole
{
    USER_SLEF,
    OTHER,
};

QT_BEGIN_NAMESPACE

namespace Ui
{
class BubbleFrame;
}

QT_END_NAMESPACE

class BubbleFrame : public QFrame
{
    Q_OBJECT

public:
    explicit BubbleFrame(ChatRole role, QWidget* parent = nullptr);
    ~BubbleFrame() override;

    void setMargin(int margin);
    void setWidget(QWidget* widget) const;

    /// mainLayout 的 contentsMargins（含气泡三角预留），供子类计算内容尺寸
    QMargins bubbleMargins() const;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    ChatRole m_role;
    int m_margin{3};

    Ui::BubbleFrame* ui;
};


#endif //CHATCLIENT_BUBBLEFRAME_H
