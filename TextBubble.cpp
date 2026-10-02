#include "TextBubble.h"

#include <qlayout.h>
#include <QTextBlock>
#include <QMargins>
#include <QtMath>

TextBubble::TextBubble(const ChatRole role, const QString& text, const QString& font, QWidget* parent)
    : BubbleFrame(role, parent)
{
    m_textEdit = new QTextEdit();
    m_textEdit->setReadOnly(true);
    m_textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_textEdit->installEventFilter(this);
    m_textEdit->setFont(QFont(font, 12));
    setPlainText(text);
    setWidget(m_textEdit);
    initStyleSheet();
}

bool TextBubble::eventFilter(QObject* watched, QEvent* event)
{
    if (m_textEdit == watched && event->type() == QEvent::Paint)
    {
        adjustTextHeight();
    }
    return BubbleFrame::eventFilter(watched, event);
}

void TextBubble::adjustTextHeight()
{
    const auto doc_margin = m_textEdit->document()->documentMargin();
    const auto* doc = m_textEdit->document();
    qreal text_height = 0;

    for (auto it = doc->begin(); it != doc->end(); it = it.next())
    {
        text_height += it.layout()->boundingRect().height();
    }
    // 用 mainLayout 的真实上下边距（this->layout() 是外层匿名布局，margins 为 0）
    const QMargins margins = bubbleMargins();
    setFixedHeight(qCeil(text_height) + doc_margin * 2 + margins.top() + margins.bottom());
}

void TextBubble::setPlainText(const QString& text)
{
    m_textEdit->setPlainText(text);
    const auto doc_margin = m_textEdit->document()->documentMargin();
    const QMargins margins = bubbleMargins();
    const QFontMetricsF fm(m_textEdit->font());
    const auto* doc = m_textEdit->document();
    int max_width = 0;

    for (auto it = doc->begin(); it != doc->end(); it = it.next())
    {
        max_width = std::max(max_width, static_cast<int>(fm.horizontalAdvance(it.text())));
    }

    setMaximumWidth(max_width + doc_margin * 2 + margins.left() + margins.right() + 1);
}

void TextBubble::initStyleSheet() const
{
    m_textEdit->setStyleSheet("QTextEdit{background: transparent; border: none;}");
}
