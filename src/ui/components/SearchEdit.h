#pragma once

#include <QLineEdit>

class SearchEdit : public QLineEdit
{
	Q_OBJECT
public:
	SearchEdit(QWidget* parent = nullptr);
	void setMaxLength(size_t max_len);

private:
	void focusOutEvent(QFocusEvent* event) override;

private:
	void limitTextLength(QString text);
};

