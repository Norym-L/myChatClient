import QtQuick
import QtQuick.Shapes

/*!
    \qmltype ZSkewRect
    \inqmlmodule ZmdUI
    \brief 平行四边形图元：上下边平行，左右边按 \l skew 倾斜。

    用于副标题黑块、标签、选中高亮块等"斜切"元素。

    \qml
    ZSkewRect { width: 240; height: 34; skew: 14; color: Theme.subBg }
    \endqml
*/
Shape {
    id: root

    /*! 填充色 */
    property color color: "transparent"
    /*! 描边色 */
    property color strokeColor: "transparent"
    /*! 描边宽度 */
    property real strokeWidth: 0
    /*! 倾斜量：顶边相对底边右移的距离，正值 = 右上倾 */
    property real skew: 10

    antialiasing: true
    preferredRendererType: Shape.CurveRenderer

    readonly property real _p: strokeWidth / 2
    readonly property real _w: Math.max(0, width - strokeWidth)
    readonly property real _h: Math.max(0, height - strokeWidth)

    ShapePath {
        strokeColor: root.strokeColor
        strokeWidth: root.strokeWidth
        fillColor: root.color
        joinStyle: ShapePath.MiterJoin
        capStyle: ShapePath.FlatCap

        startX: root._p + root.skew
        startY: root._p
        PathLine { x: root._p + root._w;               y: root._p }
        PathLine { x: root._p + root._w - root.skew;   y: root._p + root._h }
        PathLine { x: root._p;                         y: root._p + root._h }
        PathLine { x: root._p + root.skew;             y: root._p }
    }
}
