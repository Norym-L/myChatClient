import QtQuick
import QtQuick.Controls.Basic

/*!
    \qmltype ZSwitch
    \inqmlmodule ZmdUI
    \brief 开关：矩形滑轨 + 方形滑块，打开时滑轨填充荧光黄。

    没有圆角、没有阴影。滑块是切角方块，滑动用一条快速缓动，
    符合"机械拨杆"的手感。

    \qml
    ZSwitch { text: "自动登录"; checked: true }
    \endqml
*/
Switch {
    id: control

    /*! 附属说明（等宽小字），显示在主标签下方 */
    property string note: ""
    /*! 滑轨宽度 */
    property int trackWidth: 62

    // ── 几何常量：滑块与轨道的切角关系，也是"左下角对不齐"那条问题的正解 ──
    /*! 滑块相对轨道内边缘的内缩量 */
    readonly property int knobInset: 2
    /*! 轨道右上切角 */
    readonly property int trackCutTopRight: 8
    /*! 轨道左下切角 */
    readonly property int trackCutBottomLeft: 4
    /*!
        滑块右上切角 = 轨道同侧切角 − 内缩量。
        必须比轨道小一档，两条斜边才**平行**；等大或更大就会横穿轨道斜边，
        在角上露出一块浅色缝隙（看起来就是"没对齐"）。
    */
    readonly property int knobCutTopRight: trackCutTopRight - knobInset
    /*! 滑块左下切角，规则同上 */
    readonly property int knobCutBottomLeft: trackCutBottomLeft - knobInset

    hoverEnabled: true
    spacing: Theme.spacingMd
    implicitHeight: Theme.dp(Theme.controlHeightSm)
    leftPadding: 0
    rightPadding: 0

    indicator: Item {
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: control.trackWidth
        height: 26

        // ── 滑轨 ──
        ZCutRect {
            anchors.fill: parent
            color: control.checked
                   ? (control.enabled ? Theme.accent : Theme.surfaceMuted)
                   : Theme.surfaceSunken
            strokeColor: control.enabled ? Theme.ink : Theme.lineSoft
            strokeWidth: Theme.border
            cutTopRight: control.trackCutTopRight
            cutBottomLeft: control.trackCutBottomLeft

            // 轨道内的刻度线：机械量程感
            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.leftMargin: 26
                spacing: 4
                Repeater {
                    model: 5
                    Rectangle {
                        width: 1
                        height: 8
                        color: control.checked ? Theme.textOnAccent : Theme.lineSoft
                        opacity: 0.5
                    }
                }
            }
        }

        // ── 滑块 ──
        // 切角取值见上面的几何常量：滑块 = 轨道 − 内缩量，两条斜边平行、间距均匀。
        // 之前左下角是 6px 切角、轨道是 4px，滑块的斜边直接横穿轨道的斜边，
        // 在左下角露出一道浅色缝隙 —— 看起来就是"没对齐"。
        ZCutRect {
            id: knob
            width: 22
            height: 22
            y: control.knobInset
            x: control.checked ? parent.width - width - control.knobInset : control.knobInset
            // 选中时轨道是强调黄 → 滑块要用"强调色上的文字色"（亮色=黑 / 暗色=深炭），
            // 用 Theme.ink 的话暗色下会变成浅灰滑块压在黄轨道上，糊成一团
            color: control.enabled
                   ? (control.checked ? Theme.textOnAccent : Theme.subBgAlt)
                   : Theme.lineSoft
            cutTopRight: control.knobCutTopRight
            cutBottomLeft: control.knobCutBottomLeft


            Behavior on x {
                NumberAnimation { duration: Theme.durBase; easing.type: Theme.easingEnter }
            }

            // 滑块上的两道刻痕
            Column {
                anchors.centerIn: parent
                spacing: 3
                Repeater {
                    model: 2
                    Rectangle {
                        width: 8
                        height: 1.5
                        color: control.checked ? Theme.accent : Theme.surface
                        opacity: 0.9
                    }
                }
            }
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
        }
    }
}
