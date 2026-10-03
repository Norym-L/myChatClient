#pragma once

#include <QLabel>
#include "global.h"

class ClickedLabel : public QLabel
{
    Q_OBJECT

public:
    explicit ClickedLabel(QWidget* parent);

    /**
     * 设置状态样式
     * @param normal 默认样式
     * @param normal_hover 悬浮样式
     * @param normal_press 按下样式
     * @param select 选择样式
     * @param select_hover 选择时悬浮样式
     * @param select_press 选择按下样式
     */
    void setState(
        const QString& normal = "", const QString& normal_hover = "", const QString& normal_press = "",
        const QString& select = "", const QString& select_hover = "", const QString& select_press = "");
    [[nodiscard]] ClickLabelState getCurrentState() const;

    void setCurrentState(ClickLabelState state);
    void resetState();

protected:
    /// 鼠标点击事件
    void mousePressEvent(QMouseEvent* event) override;
    /// 鼠标释放事件
    void mouseReleaseEvent(QMouseEvent* event) override;
    /// 鼠标进入事件
    void enterEvent(QEnterEvent* event) override;
    /// 鼠标离开事件
    void leaveEvent(QEvent* event) override;

private:
    QString m_normal;
    QString m_normalHover;
    QString m_normalPress;

    QString m_selected;
    QString m_selectedHover;
    QString m_selectedPress;

    ClickLabelState m_currentState;

signals:
    /// 按下信号
    void clicked();
    void clickedWithNameAndState(QString, ClickLabelState);

    /// 释放信号
    void released();
    void releasedWithNameAndState(QString, ClickLabelState);
};
