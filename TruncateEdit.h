#pragma once

#include <QLineEdit>

class TruncateEdit : public QLineEdit
{
    Q_OBJECT

public:
    explicit TruncateEdit(QWidget *parent = nullptr);
    void setMaxLen(int max_len);

protected:
    void focusOutEvent(QFocusEvent *event) override;

private:
    void limitTextLength(QString text);

    int m_maxLen{0};

signals:
    void sigFocusOut();
};
