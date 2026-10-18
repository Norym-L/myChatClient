import QtQuick
import QtQuick.Controls.Basic

/*!
    \qmltype ZSlider
    \inqmlmodule ZmdUI
    \brief 滑块：黑色导轨 + 亮黄填充 + 刻度线 + 切角方形把手。

    \l showTicks 打开刻度线；\l label 为左侧铭牌，\l valueSuffix 会拼在读数后面。
    读数以等宽字体显示在右下角，不带小圆点，只用一条黑色短横线分隔。

    \qml
    ZSlider { from: 0; to: 100; value: 62; label: "负载上限"; valueSuffix: "%" }
    \endqml
*/
Slider {
    id: control

    /*! 左侧铭牌，空则隐藏 */
    property string label: ""
    /*! 读数后缀 */
    property string valueSuffix: ""
    /*! 是否绘制刻度线 */
    property bool showTicks: true
    /*! 读数小数位 */
    property int decimals: 0

    hoverEnabled: true
    // Basic 样式给 Slider 默认 padding: 6。四个方向都必须显式归零，
    // 否则手柄会按"缩小后的可用高度"居中，而导轨按整个高度居中 —— 两者错位，
    // 表现为"滑块偏高"（差的正好是 topPadding）。
    padding: 0

    // ── 垂直方向切成三条互不重叠的带 ──
    // 手柄是有高度的实体（20px），它一旦在垂直方向自由居中就会压到铭牌和读数上
    // （滑到两端时正好落在同一列）。所以改成：顶部铭牌带 / 中间手柄带 / 底部读数带，
    // 三条带各自占位，手柄只在中间带里活动。
    /*! 顶部铭牌带高度 */
    readonly property int bandTop: label.length > 0 ? 17 : 5
    /*! 中间手柄活动带高度 */
    readonly property int bandTrack: 24
    /*! 底部读数带高度 */
    readonly property int bandBottom: 20

    implicitHeight: bandTop + bandTrack + bandBottom

    /*! 垂直中心：手柄、导轨、刻度全部以它为基准 */
    readonly property real centerY: bandTop + bandTrack / 2
    readonly property real trackY: centerY - 4
    readonly property string valueText: value.toFixed(decimals) + valueSuffix

    background: Item {
        // ── 刻度线：导轨下方 ──
        Canvas {
            id: ticks
            anchors.fill: parent
            visible: control.showTicks
            renderStrategy: Canvas.Immediate
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onVisibleChanged: requestPaint()
            Connections {
                target: control
                function onValueChanged() { ticks.requestPaint() }
            }
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var w = width
                var h = height
                // 刻度紧贴导轨下沿、长度收短 —— 必须留在中间手柄带之内，
                // 否则会伸进底部读数带，和百分比数字叠在一起
                var y0 = control.trackY + 10
                var n = 11
                ctx.lineWidth = 1.2
                for (var i = 0; i < n; i++) {
                    var x = Math.round(w * i / (n - 1)) + 0.5
                    var passed = (i / (n - 1)) <= control.visualPosition
                    ctx.strokeStyle = passed ? Theme.ink : Theme.lineSoft
                    ctx.beginPath()
                    ctx.moveTo(x, y0)
                    ctx.lineTo(x, y0 + (i % 5 === 0 ? 5 : 3))
                    ctx.stroke()
                }
            }
        }

        // ── 导轨（空槽）──
        Rectangle {
            x: 0
            y: control.trackY
            width: parent.width
            height: 8
            color: Theme.surfaceSunken
            border.color: Theme.ink
            border.width: 1
        }

        // ── 已填充部分：亮黄（宽度 = 把手中心位置）──
        Rectangle {
            x: 1
            y: control.trackY + 1
            width: Math.max(0, control.visualPosition * (control.availableWidth - 16) + 8)
            height: 6
            color: control.enabled ? Theme.accent : Theme.lineSoft
        }

        // ── 左侧铭牌 ──
        Text {
            anchors.left: parent.left
            anchors.top: parent.top
            visible: control.label.length > 0
            text: control.label + " /"
            font: Theme.fontMonoSm
            color: Theme.muted
        }

        // ── 右下角读数 ──
        Row {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            spacing: Theme.spacingXs
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 2
                color: Theme.ink
            }
            Text {
                text: control.valueText
                font: Theme.fontMonoMd
                color: Theme.ink
            }
        }
    }

    handle: ZCutRect {
        id: handleItem
        x: control.visualPosition * (control.availableWidth - width)
        // 只在中间手柄带里居中：其他元素各有各的带，不会互相压到
        y: control.centerY - height / 2
        width: 14
        height: 20
        color: control.pressed ? Theme.ink : Theme.accent
        // 把手底色是强调黄 → 描边用强调色上的文字色，暗色下才不会浅色压浅色
        strokeColor: Theme.textOnAccent
        strokeWidth: Theme.border
        cutTopRight: 5
        cutBottomLeft: 5

        // 把手上的三条刻痕
        Column {
            anchors.centerIn: parent
            spacing: 2
            Repeater {
                model: 3
                Rectangle {
                    width: 5
                    height: 1.5
                    color: control.pressed ? Theme.accent : Theme.textOnAccent
                    opacity: 0.7
                }
            }
        }
    }
}
