import QtQuick

/*!
    \qmltype ZLineChart
    \inqmlmodule ZmdUI
    \brief 极简折线图：工程网格 + 亮黄数据线 + 黑色方形数据点。

    纯 Canvas 绘制，无第三方依赖。数据点少（< 200）时性能足够。
    亮黄数据线外面套一层黑描边保证在白底上的对比度。

    \qml
    ZLineChart {
        width: 480; height: 200
        values: [12, 18, 15, 26, 22, 31, 28, 36]
        labels: ["00", "01", "02", "03", "04", "05", "06", "07"]
        caption: "RESPONSE TIME / SEC"
        suffix: " ms"
    }
    \endqml
*/
Item {
    id: root

    /*! 数据（数字数组） */
    property var values: []
    /*! X 轴标签，长度不足时用序号补 */
    property var labels: []
    /*! 左上角等宽说明 */
    property string caption: ""
    /*! 读数后缀 */
    property string suffix: ""
    /*! 数据线颜色（默认亮黄） */
    property color lineColor: Theme.accent
    /*! 是否画数据点 */
    property bool showDots: true
    /*! 是否显示右侧当前值 */
    property bool showLatest: true
    /*! Y 轴网格行数 */
    property int gridRows: 4

    implicitWidth: 480
    implicitHeight: 200

    readonly property real plotLeft: 44
    readonly property real plotRight: 16
    readonly property real plotTop: caption.length > 0 ? 34 : 16
    readonly property real plotBottom: 26

    readonly property real plotWidth: Math.max(0, width - plotLeft - plotRight)
    readonly property real plotHeight: Math.max(0, height - plotTop - plotBottom)

    readonly property real maxValue: {
        var m = 0
        for (var i = 0; i < values.length; i++)
            m = Math.max(m, Number(values[i]))
        return m <= 0 ? 1 : m * 1.15
    }

    // ── 外框 ──
    ZCutRect {
        anchors.fill: parent
        color: "transparent"
        strokeColor: Theme.lineSoft
        strokeWidth: 1
        cutTopRight: Theme.cutMd
        cutBottomLeft: Theme.cutSm
    }

    Canvas {
        id: chart
        anchors.fill: parent
        renderStrategy: Canvas.Immediate

        // 任何影响绘制的输入变化都要重绘
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        Connections {
            target: root
            function onValuesChanged() { chart.requestPaint() }
            function onLabelsChanged() { chart.requestPaint() }
            function onLineColorChanged() { chart.requestPaint() }
        }
        // 亮暗主题切换后网格/坐标文字颜色要跟着重画
        Connections {
            target: Theme
            function onIsDarkChanged() { chart.requestPaint() }
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.reset()

            var w = width
            var h = height
            var n = root.values.length
            if (w <= 0 || h <= 0)
                return

            var x0 = root.plotLeft
            var y0 = root.plotTop
            var pw = root.plotWidth
            var ph = root.plotHeight
            var vmax = root.maxValue

            // ── 背景网格 ──
            ctx.lineWidth = 1
            ctx.strokeStyle = Theme.gridLineMajor
            ctx.beginPath()
            for (var r = 0; r <= root.gridRows; r++) {
                var gy = Math.round(y0 + ph * r / root.gridRows) + 0.5
                ctx.moveTo(x0, gy)
                ctx.lineTo(x0 + pw, gy)
                // 网格刻度短线
                ctx.moveTo(x0 - 5, gy)
                ctx.lineTo(x0, gy)
            }
            ctx.stroke()

            // Y 轴刻度值（等宽小字）
            ctx.fillStyle = Theme.muted
            ctx.font = "10px " + Theme.familyMono
            ctx.textAlign = "right"
            ctx.textBaseline = "middle"
            for (var r2 = 0; r2 <= root.gridRows; r2++) {
                var gy2 = y0 + ph * r2 / root.gridRows
                var val = Math.round(vmax * (1 - r2 / root.gridRows))
                ctx.fillText(String(val), x0 - 9, gy2)
            }

            if (n === 0)
                return

            // ── X 轴基线 ──
            ctx.strokeStyle = Theme.line
            ctx.lineWidth = 2
            ctx.beginPath()
            ctx.moveTo(x0, y0 + ph + 0.5)
            ctx.lineTo(x0 + pw, y0 + ph + 0.5)
            ctx.stroke()

            // ── 数据点坐标 ──
            var step = n > 1 ? pw / (n - 1) : 0
            var pts = []
            for (var i = 0; i < n; i++) {
                pts.push({
                    x: x0 + step * i,
                    y: y0 + ph - ph * (Number(root.values[i]) / vmax)
                })
            }

            // ── 面积：用亮黄低透明度平涂（不做渐变）──
            ctx.beginPath()
            ctx.moveTo(pts[0].x, y0 + ph)
            for (var a = 0; a < n; a++)
                ctx.lineTo(pts[a].x, pts[a].y)
            ctx.lineTo(pts[n - 1].x, y0 + ph)
            ctx.closePath()
            ctx.fillStyle = Qt.rgba(root.lineColor.r, root.lineColor.g, root.lineColor.b, 0.22)
            ctx.fill()

            // ── 折线：先黑后黄，保证白底上的对比度 ──
            ctx.lineJoin = "miter"
            ctx.lineCap = "butt"
            ctx.beginPath()
            ctx.moveTo(pts[0].x, pts[0].y)
            for (var b = 1; b < n; b++)
                ctx.lineTo(pts[b].x, pts[b].y)
            // 描边底用 inkBlock：亮色下是近黑（和以前一样），暗色下是比面板更深的黑，
            // 黄线才会"亮"起来；用 ink 的话暗色下会变成浅灰描边，看着像灰线
            ctx.strokeStyle = Theme.inkBlock
            ctx.lineWidth = 5
            ctx.stroke()
            ctx.strokeStyle = root.lineColor
            ctx.lineWidth = 2.5
            ctx.stroke()

            // ── 数据点：黑色方块 ──
            if (root.showDots) {
                for (var c = 0; c < n; c++) {
                    ctx.fillStyle = Theme.inkBlock
                    ctx.fillRect(pts[c].x - 3, pts[c].y - 3, 6, 6)
                    ctx.fillStyle = root.lineColor
                    ctx.fillRect(pts[c].x - 1.5, pts[c].y - 1.5, 3, 3)
                }
            }

            // ── X 轴标签 ──
            ctx.fillStyle = Theme.muted
            ctx.font = "10px " + Theme.familyMono
            ctx.textAlign = "center"
            ctx.textBaseline = "top"
            for (var d = 0; d < n; d++) {
                // 点太密时只标偶数位
                if (n > 8 && d % 2 !== 0)
                    continue
                var lb = (root.labels.length > d) ? String(root.labels[d]) : String(d)
                ctx.fillText(lb, pts[d].x, y0 + ph + 8)
            }

            // ── 最新值标注 ──
            if (root.showLatest) {
                var last = pts[n - 1]
                var txt = String(root.values[n - 1]) + root.suffix
                ctx.font = "11px " + Theme.familyMono
                var tw = ctx.measureText(txt).width
                var bx = Math.min(last.x + 8, w - tw - 14)
                var by = Math.max(4, last.y - 24)

                // 黑底标签块
                ctx.fillStyle = Theme.subBg
                ctx.fillRect(bx, by, tw + 12, 18)
                // 亮黄左边缘
                ctx.fillStyle = root.lineColor
                ctx.fillRect(bx, by, 3, 18)
                // 白字
                ctx.fillStyle = Theme.textOnSub
                ctx.textAlign = "left"
                ctx.textBaseline = "middle"
                ctx.fillText(txt, bx + 8, by + 10)

                // 最新点的十字准星
                ctx.strokeStyle = Theme.ink
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(last.x, y0)
                ctx.lineTo(last.x, y0 + ph)
                ctx.stroke()
            }
        }
    }

    // ── 左上角说明 ──
    Row {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.leftMargin: Theme.spacingMd
        anchors.topMargin: Theme.spacingSm
        spacing: Theme.spacingSm
        visible: root.caption.length > 0

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 14
            height: 3
            color: root.lineColor
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.caption
            font: Theme.fontMonoSm
            color: Theme.muted
        }
    }
}
