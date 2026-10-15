import QtQuick
import QtQuick.Controls.Basic

/*!
    \qmltype ZRadioButton
    \inqmlmodule ZmdUI
    \brief 单选按钮：菱形指示灯（设计语言里的"选中=亮黄填充几何图形"）。

    指示器由 ZCutRect 四个角全部斜切一半形成菱形，不需要额外的图形资源。
    选中时黄色菱形内嵌一个黑色小方点。

    \qml
    ZRadioButton { text: "全量同步"; checked: true }
    \endqml
*/
RadioButton {
    id: control

    hoverEnabled: true
    spacing: Theme.spacingSm
    implicitHeight: Theme.dp(Theme.controlHeightSm)
    leftPadding: 0
    rightPadding: 0

    indicator: Item {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 20
        height: 20

        // 菱形外框
        ZCutRect {
            anchors.fill: parent
            color: control.checked ? Theme.accent : Theme.surface
            strokeColor: control.enabled ? Theme.ink : Theme.lineSoft
            strokeWidth: Theme.border
            // 四角各切一半 = 菱形
            cutTopLeft: width / 2
            cutTopRight: width / 2
            cutBottomLeft: width / 2
            cutBottomRight: width / 2
        }

        // 悬停外圈
        ZCutRect {
            anchors.fill: parent
            anchors.margins: -4
            visible: control.enabled && control.hovered && !control.checked
            color: "transparent"
            strokeColor: Theme.accent
            strokeWidth: 2
            cutTopLeft: width / 2
            cutTopRight: width / 2
            cutBottomLeft: width / 2
            cutBottomRight: width / 2
        }

        // 选中内点
        ZCutRect {
            anchors.centerIn: parent
            width: 8
            height: 8
            visible: control.checked
            color: Theme.textOnAccent
            cutTopLeft: 2
            cutBottomRight: 2
        }
    }

    contentItem: Text {
        leftPadding: control.indicator.width + control.spacing
        text: control.text
        font: Theme.fontStrong
        color: control.enabled ? Theme.ink : Theme.muted
        verticalAlignment: Text.AlignVCenter
    }
}
