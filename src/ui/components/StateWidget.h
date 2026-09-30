#ifndef CHATCLIENT_STATEWIDGET_H
#define CHATCLIENT_STATEWIDGET_H

#include <QWidget>
#include <QLabel>
#include "app/global.h"

class StateWidget : public QWidget
{
    Q_OBJECT

public:
    explicit StateWidget(QWidget* parent = nullptr);

    void setState(const QString& normal = "normal", const QString& normal_hover = "normal_hover", const QString& normal_pressed = "normal_pressed",
                  const QString& selected = "selected", const QString& selected_hover = "selected_hover", const QString& selected_pressed = "selected_pressed");
    [[nodiscard]] ClickLabelState getCurrentState() const;
    void clearState();

    void setSelected(bool selected);
    /// 添加🔴红点显示
    void addRedPoint();
    void showRedPoint(bool show = true) const;
    void setDefautState(ClickLabelState state = ClickLabelState::NORMAL);

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_normal;
    QString m_normalHover;
    QString m_normalPressed;
    QString m_selected;
    QString m_selectedHover;
    QString m_selectedPressed;

    ClickLabelState m_currentState;
    QLabel* m_redPoint;

signals:
    void clicked();
    void clickedOnSelected(); // 选中状态下点击
    void released();
};


#endif //CHATCLIENT_STATEWIDGET_H
