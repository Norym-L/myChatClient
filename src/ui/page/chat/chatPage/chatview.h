#ifndef CHATCLIENT_CHATVIEW_H
#define CHATCLIENT_CHATVIEW_H

#include <QVBoxLayout>
#include <QWidget>
#include <QScrollArea>

QT_BEGIN_NAMESPACE

namespace Ui
{
class ChatView;
}

QT_END_NAMESPACE

class ChatView : public QWidget
{
    Q_OBJECT

public:
    explicit ChatView(QWidget* parent = nullptr);
    ~ChatView() override;

    void appendChatItemAtTail(QWidget* item);           // 尾插
    void appendChatItemAtHead(QWidget* item);           // 头插
    void insertChatItem(QWidget* befor, QWidget* item); // 中间插

    bool eventFilter(QObject* watched, QEvent* event) override;

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void slotOnVScrollBarMoved(int min, int max); // 滑动滚动条触发

private:
    void initScrollBar();
    void initStyleSheet();

    bool m_isAppendedChatItem{false}; // 是否添加了新的记录
    Ui::ChatView* ui;
};


#endif //CHATCLIENT_CHATVIEW_H
