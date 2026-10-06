#ifndef CHATCLIENT_BUBBLEFRAME_H
#define CHATCLIENT_BUBBLEFRAME_H

#include <QColor>
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
    /// 气泡颜色由主题 QSS 经 qproperty 注入（themes/dark.qss / light.qss 的
    /// BubbleFrame 规则）；缺省值沿用旧配色，QSS 缺失时行为不变
    Q_PROPERTY(QColor bubbleColorSelf MEMBER m_bubbleColorSelf)
    Q_PROPERTY(QColor bubbleColorOther MEMBER m_bubbleColorOther)

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

    QColor m_bubbleColorSelf{59, 59, 59};
    QColor m_bubbleColorOther{59, 59, 59};

    Ui::BubbleFrame* ui;
};


#endif //CHATCLIENT_BUBBLEFRAME_H
