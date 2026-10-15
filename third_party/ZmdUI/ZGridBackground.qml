import QtQuick

/*!
    \qmltype ZGridBackground
    \inqmlmodule ZmdUI
    \brief 工程图纸网格背景（细网格 + 主刻度 + 十字星标记），纯 Canvas 绘制。

    整块铺底，作为页面根背景使用。主刻度交叉点会画小十字（+），
    对应设计语言里的"科技感细节"。

    \qml
    ZGridBackground {
        anchors.fill: parent
        bgColor: Theme.bg
        minorColor: Theme.gridLine
        majorColor: Theme.gridLineMajor
        crossColor: Theme.lineSoft
    }
    \endqml

    \note 绘制是一次性的（尺寸/配色变化才重绘），在 4K 下也不会掉帧。
*/
Canvas {
    id: root

    /*! 底色，一般传 Theme.bg */
    property color bgColor: "#F2F2F2"
    /*! 细网格线色 */
    property color minorColor: "#E4E4E4"
    /*! 主网格线色（每 majorStep 一根） */
    property color majorColor: "#D2D2D2"
    /*! 十字星颜色 */
    property color crossColor: "#C4C4C4"

    /*! 细网格步长 */
    property real minorStep: 12
    /*! 主网格步长（应当是 minorStep 的整数倍） */
    property real majorStep: 60
    /*! 是否绘制十字星 */
    property bool showCross: true
    /*! 每隔几个主交叉点画一个十字（越大越稀疏） */
    property int crossEvery: 2
    /*! 十字臂长 */
    property real crossArm: 3.5

    readonly property real _ratio: Math.max(2, Math.round(majorStep / Math.max(1, minorStep)))

    renderStrategy: Canvas.Immediate

    onBgColorChanged: requestPaint()
    onMinorColorChanged: requestPaint()
    onMajorColorChanged: requestPaint()
    onCrossColorChanged: requestPaint()
    onMinorStepChanged: requestPaint()
    onMajorStepChanged: requestPaint()
    onShowCrossChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()

        var w = width
        var h = height
        if (w <= 0 || h <= 0)
            return

        // 1) 平涂底色：设计规范要求避免大面积渐变
        ctx.fillStyle = bgColor
        ctx.fillRect(0, 0, w, h)

        // 2) 细网格 —— 跳过与主网格重合的行列，避免深色叠加
        ctx.lineWidth = 1
        ctx.strokeStyle = minorColor
        ctx.beginPath()
        var i = 0
        for (var x = 0; x <= w; x += minorStep, i++) {
            if (i % _ratio === 0)
                continue
            // +0.5 让 1px 线落在像素中心，不糊
            ctx.moveTo(Math.round(x) + 0.5, 0)
            ctx.lineTo(Math.round(x) + 0.5, h)
        }
        i = 0
        for (var y = 0; y <= h; y += minorStep, i++) {
            if (i % _ratio === 0)
                continue
            ctx.moveTo(0, Math.round(y) + 0.5)
            ctx.lineTo(w, Math.round(y) + 0.5)
        }
        ctx.stroke()

        // 3) 主网格
        ctx.lineWidth = 1.2
        ctx.strokeStyle = majorColor
        ctx.beginPath()
        for (var mx = 0; mx <= w; mx += majorStep) {
            ctx.moveTo(Math.round(mx) + 0.5, 0)
            ctx.lineTo(Math.round(mx) + 0.5, h)
        }
        for (var my = 0; my <= h; my += majorStep) {
            ctx.moveTo(0, Math.round(my) + 0.5)
            ctx.lineTo(w, Math.round(my) + 0.5)
        }
        ctx.stroke()

        // 4) 主交叉点上的小十字星（+）
        if (!showCross)
            return
        ctx.lineWidth = 1.2
        ctx.strokeStyle = crossColor
        ctx.beginPath()
        var ci = 0
        for (var cx = 0; cx <= w; cx += majorStep) {
            var cj = 0
            for (var cy = 0; cy <= h; cy += majorStep) {
                if (ci % crossEvery === 0 && cj % crossEvery === 0) {
                    ctx.moveTo(cx - crossArm, cy)
                    ctx.lineTo(cx + crossArm, cy)
                    ctx.moveTo(cx, cy - crossArm)
                    ctx.lineTo(cx, cy + crossArm)
                }
                cj++
            }
            ci++
        }
        ctx.stroke()
    }
}
