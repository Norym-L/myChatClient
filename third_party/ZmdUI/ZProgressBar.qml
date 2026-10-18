import QtQuick
import QtQuick.Controls.Basic

/*!
    \qmltype ZProgressBar
    \inqmlmodule ZmdUI
    \brief 进度条：黑框硬边 + 亮黄填充 + 刻度线，右侧显示等宽百分数。

    \l indeterminate 打开后走"循环扫描块"动画（工程仪表的来回扫描感），
    不使用默认样式的无限流动条。

    \qml
    ZProgressBar { value: 0.62; label: "同步进度" }
    \endqml
*/
ProgressBar {
    id: control

    /*! 左侧铭牌 */
    property string label: ""
    /*! 是否显示右侧百分数 */
    property bool showValue: true
    /*! 是否绘制刻度线 */
    property bool showTicks: true

    implicitHeight: Theme.dp(label.length > 0 ? 44 : 26)
    padding: 0

    background: Item {
        // 轨道
        ZCutRect {
            id: rail
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: 18
            color: Theme.surfaceSunken
            strokeColor: Theme.ink
            strokeWidth: Theme.border
            cutTopRight: 7
            cutBottomLeft: 3
        }

        // 填充
        Rectangle {
            id: fill
            x: rail.x + Theme.border
            y: rail.y + Theme.border
            height: rail.height - Theme.border * 2
            width: Math.max(0, (rail.width - Theme.border * 2) * control.visualPosition)
            color: control.enabled ? Theme.accent : Theme.lineSoft
            clip: true

            // 填充块内部的斜线纹理（用亮黄上的黑色细斜线做"正在填充"的质感）
            ZHatchPattern {
                width: parent.width
                height: parent.height
                lineColor: Theme.ink
                opacity: 0.22
                spacing: 7
                lineWidth: 1
            }
        }

        // 刻度线
        Canvas {
            id: ticks
            anchors.fill: rail
            visible: control.showTicks
            renderStrategy: Canvas.Immediate
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var n = 20
                ctx.lineWidth = 1
                for (var i = 1; i < n; i++) {
                    var x = Math.round(width * i / n) + 0.5
                    ctx.strokeStyle = (i / n) <= control.visualPosition
                                     ? Qt.rgba(0, 0, 0, 0.55) : Theme.lineSoft
                    ctx.beginPath()
                    ctx.moveTo(x, height * 0.25)
                    ctx.lineTo(x, height * 0.75)
                    ctx.stroke()
                }
            }
            Connections {
                target: control
                function onVisualPositionChanged() { ticks.requestPaint() }
            }
        }

        // 不确定态：来回扫动的黑块
        Rectangle {
            visible: control.indeterminate
            width: rail.width * 0.28
            height: rail.height
            y: rail.y
            color: Theme.ink
            opacity: 0.75
            x: rail.x + (rail.width - width) * control.visualPosition
        }

        Text {
            anchors.left: parent.left
            anchors.top: parent.top
            visible: control.label.length > 0
            text: control.label + " /"
            font: Theme.fontMonoSm
            color: Theme.muted
        }
    }

    contentItem: Text {
        visible: control.showValue && !control.label
        text: control.indeterminate ? "····" : Math.round(control.value * 100) + "%"
        font: Theme.fontMonoMd
        color: Theme.ink
        horizontalAlignment: Text.AlignRight
        verticalAlignment: Text.AlignVCenter
    }
}
