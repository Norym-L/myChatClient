import QtQuick
import QtQuick.Controls.Basic

/*!
    \qmltype ZScrollBar
    \inqmlmodule ZmdUI
    \brief 滚动条：细长切角方块 + 导轨刻度线，悬停变亮黄。

    用法与原生一致，挂在 ListView / Flickable 上：
    \qml
    ListView { ScrollBar.vertical: ZScrollBar { policy: ScrollBar.AsNeeded } }
    \endqml
*/
ScrollBar {
    id: control

    hoverEnabled: true
    implicitWidth: 12
    minimumSize: 0.06
    padding: 0

    background: Item {
        // 导轨：一条细黑线 + 刻度
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 2
            height: parent.height
            color: Theme.lineSoft
            opacity: 0.5
        }
        Canvas {
            anchors.fill: parent
            renderStrategy: Canvas.Immediate
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                ctx.lineWidth = 1
                ctx.strokeStyle = Theme.lineSoft
                ctx.beginPath()
                for (var y = 0; y < height; y += 10) {
                    ctx.moveTo(width - 5, y + 0.5)
                    ctx.lineTo(width, y + 0.5)
                }
                ctx.stroke()
            }
        }
    }

    contentItem: ZCutRect {
        color: control.pressed ? Theme.ink
             : (control.hovered || control.active ? Theme.accent : Theme.lineSoft)
        cutTopRight: 4
        cutBottomLeft: 4
    }
}
