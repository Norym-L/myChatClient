import QtQuick

/*!
    \qmltype ZHatchPattern
    \inqmlmodule ZmdUI
    \brief 斜线阴影 / 斜线纹理（Diagonal Hatching），纯 Canvas 绘制。

    工程图纸上常用的"禁入区 / 特殊区"纹理。放在面板角落、对话框中央做质感。

    \qml
    ZHatchPattern {
        anchors.right: parent.right
        width: 160; height: 90
        lineColor: Theme.hatch
        spacing: 9
    }
    \endqml

    \note 只在尺寸 / 参数变化时重绘，静态使用不会持续占用 GPU / CPU。
*/
Canvas {
    id: root

    /*! 线色 */
    property color lineColor: "#D6D6D6"
    /*! 线间距（逻辑像素） */
    property real spacing: 9
    /*! 线宽 */
    property real lineWidth: 1.4
    /*! true = 竖斜线（90°），false = 横斜线（45°） */
    property bool vertical: false
    /*! true = 双向交叉成网眼 */
    property bool cross: false

    renderStrategy: Canvas.Immediate

    // 任一参数变化都要重绘
    onLineColorChanged: requestPaint()
    onSpacingChanged: requestPaint()
    onLineWidthChanged: requestPaint()
    onVerticalChanged: requestPaint()
    onCrossChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()

        var w = width
        var h = height
        if (w <= 0 || h <= 0)
            return

        var s = Math.max(3, spacing)
        ctx.lineWidth = lineWidth
        ctx.strokeStyle = lineColor
        ctx.lineCap = "butt"

        if (!vertical) {
            // 45°：从左下往右上铺线，起点从 -h 开始才能盖满左上角
            for (var x = -h; x < w + h; x += s) {
                ctx.beginPath()
                ctx.moveTo(x, 0)
                ctx.lineTo(x + h, h)
                ctx.stroke()
                if (cross) {
                    ctx.beginPath()
                    ctx.moveTo(x + h, 0)
                    ctx.lineTo(x, h)
                    ctx.stroke()
                }
            }
        } else {
            // 90°：竖向斜线
            for (var y = -w; y < h + w; y += s) {
                ctx.beginPath()
                ctx.moveTo(0, y)
                ctx.lineTo(w, y + w)
                ctx.stroke()
                if (cross) {
                    ctx.beginPath()
                    ctx.moveTo(0, y + w)
                    ctx.lineTo(w, y)
                    ctx.stroke()
                }
            }
        }
    }
}
