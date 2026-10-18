import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes

/*!
    \qmltype ZCheckBox
    \inqmlmodule ZmdUI
    \brief 复选框：斜切角方框指示灯，选中时荧光黄填充 + 黑色对勾。

    \l indeterminate 为真时显示"半选"横杠，用于父级批量选择。

    \qml
    ZCheckBox { text: "启用自动重连"; checked: true }
    \endqml
*/
CheckBox {
    id: control

    /*! 附属说明，显示在主标签下方（等宽小字） */
    property string note: ""
    /*! 半选态：父级批量选择时使用（原生 CheckBox 没有这个属性，由本组件提供） */
    property bool indeterminate: false

    hoverEnabled: true
    spacing: Theme.spacingSm
    implicitHeight: Theme.dp(Theme.controlHeightSm)
    leftPadding: 0
    rightPadding: 0

    indicator: ZCutRect {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 20
        height: 20
        color: control.checked || control.indeterminate ? Theme.accent : Theme.surface
        strokeColor: control.enabled
                     ? (control.visualFocus || control.hovered ? Theme.ink : Theme.inkSoft)
                     : Theme.lineSoft
        strokeWidth: Theme.border
        cutTopRight: 7
        cutBottomLeft: 3

        // 悬停时外圈细黄框，提示可点
        ZCutRect {
            anchors.fill: parent
            anchors.margins: -4
            visible: control.enabled && control.hovered
            color: "transparent"
            strokeColor: Theme.accent
            strokeWidth: 2
            cutTopRight: 7
            cutBottomLeft: 3
        }

        // ── 对勾：纯路径绘制，中段折角 ──
        Shape {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: 0
            width: 13
            height: 9
            visible: control.checked && !control.indeterminate
            antialiasing: true
            preferredRendererType: Shape.CurveRenderer
            ShapePath {
                strokeColor: Theme.textOnAccent
                strokeWidth: 2.6
                fillColor: "transparent"
                capStyle: ShapePath.FlatCap
                joinStyle: ShapePath.MiterJoin
                PathPolyline {
                    path: [Qt.point(0, 4), Qt.point(4.5, 8.5), Qt.point(13, 0.5)]
                }
            }
        }

        // ── 半选横杠 ──
        Rectangle {
            anchors.centerIn: parent
            width: 10
            height: 3
            visible: control.indeterminate
            color: Theme.textOnAccent
        }
    }

    contentItem: Column {
        leftPadding: control.indicator.width + control.spacing
        spacing: 1
        Text {
            text: control.text
            font: Theme.fontStrong
            color: control.enabled ? Theme.ink : Theme.muted
            verticalAlignment: Text.AlignVCenter
        }
        Text {
            visible: control.note.length > 0
            text: control.note
            font: Theme.fontMonoSm
            color: Theme.muted
            opacity: 0.85
        }
    }
}
