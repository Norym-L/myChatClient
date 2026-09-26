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

private:
    Ui::InterChatPage* ui;
};


#endif //CHATCLIENT_INTERCHATPAGE_H
