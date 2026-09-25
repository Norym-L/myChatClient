#include "ui/components/TruncateEdit.h"
TruncateEdit::TruncateEdit(QWidget *parent)
    : QLineEdit(parent)
{
    connect(this, &QLineEdit::textChanged, this, &TruncateEdit::limitTextLength);
}

void TruncateEdit::setMaxLen(const int max_len)
{
    m_maxLen = max_len;
}

void TruncateEdit::focusOutEvent(QFocusEvent *event)
{
    QLineEdit::focusOutEvent(event);
    emit sigFocusOut();
}

void TruncateEdit::limitTextLength(QString text)
{
    if (m_maxLen <= 0)
    {
        return;
    }
    const QByteArray byte_array = text.toUtf8();
    if (byte_array.size() > m_maxLen)
    {
        text = QString::fromUtf8(byte_array.left(m_maxLen));
    }

    setText(text);
}
