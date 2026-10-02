#ifndef CHATCLIENT_TEXTBUBBLE_H
#define CHATCLIENT_TEXTBUBBLE_H

#include <QTextEdit>

#include "bubbleframe.h"

class TextBubble : public BubbleFrame
{
    Q_OBJECT
public:
    explicit TextBubble(ChatRole role, const QString& text, const QString& font = "Microsoft YaHei", QWidget* parent = nullptr);

    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void adjustTextHeight();
    void setPlainText(const QString& text);
    void initStyleSheet() const;

    QTextEdit* m_textEdit{nullptr};
};


#endif //CHATCLIENT_TEXTBUBBLE_H
