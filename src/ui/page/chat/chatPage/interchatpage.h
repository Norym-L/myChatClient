#ifndef CHATCLIENT_INTERCHATPAGE_H
#define CHATCLIENT_INTERCHATPAGE_H

#include <QWidget>


QT_BEGIN_NAMESPACE

namespace Ui
{
class InterChatPage;
}

QT_END_NAMESPACE

class QTextEdit;

class InterChatPage : public QWidget
{
    Q_OBJECT

public:
    explicit InterChatPage(QWidget* parent = nullptr);
    ~InterChatPage() override;

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initButton();
    /// 把按钮区摆到输入框右下角（输入框尺寸变化后需重新调用）
    void updateButtonPos() const;

    Ui::InterChatPage* ui;
};


#endif //CHATCLIENT_INTERCHATPAGE_H
