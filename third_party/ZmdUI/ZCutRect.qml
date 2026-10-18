import QtQuick
import QtQuick.Shapes

/*!
    \qmltype ZCutRect
    \inqmlmodule ZmdUI
    \brief 支持四角独立斜切的实心 / 描边矩形，用来替代带圆角的 Rectangle。

    这是整套设计语言的地基：任何"卡片、按钮、输入框"的硬朗切角都由它绘制。
    切角走 Shape 路径（MiterJoin，不加圆角），因此边缘是干净的直角折线。

    \qml
    ZCutRect {
        width: 200; height: 48
        color: Theme.accent
        strokeColor: Theme.ink
        strokeWidth: Theme.border
        cutTopRight: Theme.cutMd
        cutBottomLeft: Theme.cutSm
    }
    \endqml

    \note Shape 的 implicitWidth/implicitHeight 由路径包围盒反算，请不要给它们
          绑定表达式（会被 C++ 侧覆盖）。使用处请显式给出 width/height 或用 anchors 约束。
          把四个角的切角都设成 min(w,h)/2 就会得到一个菱形（单选按钮的指示器就靠这个）。
*/
Shape {
    id: root

    /*! 填充色 */
    property color color: "transparent"
    /*! 描边色 */
    property color strokeColor: "transparent"
    /*! 描边宽度（居中于路径，内部已按 1/2 内缩，保证外轮廓正好贴住 width/height） */
    property real strokeWidth: 0

    /*! 左上角斜切尺寸 */
    property real cutTopLeft: 0
    /*! 右上角斜切尺寸 */
    property real cutTopRight: 0
    /*! 右下角斜切尺寸 */
    property real cutBottomRight: 0
    /*! 左下角斜切尺寸 */
    property real cutBottomLeft: 0

    /*! 反向斜切：为 true 时切角方向反转，用于制造"咬合"错位感 */
    property bool invertCuts: false

    antialiasing: true
    preferredRendererType: Shape.CurveRenderer

    // ── 内部几何：0.5 像素对齐，避免 1px 描边发虚 ──
    readonly property real _p: strokeWidth / 2
    readonly property real _w: Math.max(0, width - strokeWidth)
    readonly property real _h: Math.max(0, height - strokeWidth)
    readonly property real _x0: _p
    readonly property real _y0: _p
    readonly property real _x1: _p + _w
    readonly property real _y1: _p + _h

    // 切角不得超过短边的一半，否则路径自交
    function _clamp(v) {
        return Math.max(0, Math.min(v, Math.min(_w, _h) / 2))
    }

    readonly property real _tl: _clamp(cutTopLeft)
    readonly property real _tr: _clamp(cutTopRight)
    readonly property real _br: _clamp(cutBottomRight)
    readonly property real _bl: _clamp(cutBottomLeft)

    ShapePath {
        strokeColor: root.strokeColor
        strokeWidth: root.strokeWidth
        fillColor: root.color
        joinStyle: ShapePath.MiterJoin
        capStyle: ShapePath.FlatCap

        startX: root._x0 + root._tl
        startY: root._y0

        // 上边 → 右上切角
        PathLine { x: root._x1 - root._tr; y: root._y0 }
        PathLine { x: root._x1;            y: root._y0 + root._tr }
        // 右边
        PathLine { x: root._x1;            y: root._y1 - root._br }
        // 右下切角 → 下边
        PathLine { x: root._x1 - root._br; y: root._y1 }
        PathLine { x: root._x0 + root._bl; y: root._y1 }
        // 左下切角 → 左边
        PathLine { x: root._x0;            y: root._y1 - root._bl }
        PathLine { x: root._x0;            y: root._y0 + root._tl }
        // 回到起点，闭合
        PathLine { x: root._x0 + root._tl; y: root._y0 }
    }
}
