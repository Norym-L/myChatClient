import QtQuick
import QtQuick.Controls.Basic

/*!
    \qmltype ZToolTip
    \inqmlmodule ZmdUI
    \brief 工具提示：深灰底白字，小号等宽字体，左侧一条亮黄竖边。

    一般不直接手写，给按钮设 \l ZButton::tooltip 即可自动挂上：
    \qml
    ZButton { text: "同步余额"; tooltip: "同步最近 24 小时余额变动" }
    \endqml

    \note 亮黄竖边画在 background 里、文字用 contentItem 的 padding 定位。
          两者若各算一套间距（一个用 anchors、一个用 padding）就会错位，
          所以这里只保留一套：竖边固定贴左，文字靠 leftPadding 让开。
*/
ToolTip {
    id: control

    delay: 300
    timeout: 6000
    padding: 0

    /*! 背景斜切量，与 leftPadding 的留白量对应 */
    readonly property int skewAmount: 7

    // 贴宿主正下方、水平居中。用 implicitWidth 而不是 width：
    // popup 的最终宽度就是由 implicitWidth 决定的，用 width 会在首帧取到 0。
    x: parent ? Math.round((parent.width - implicitWidth) / 2) : 0
    y: parent ? Math.round(parent.height + 6) : 0

    leftPadding: skewAmount + 12
    rightPadding: 14
    topPadding: 7
    bottomPadding: 7

    background: Item {
        // 深灰平行四边形底板
        ZSkewRect {
            anchors.fill: parent
            skew: control.skewAmount
            color: Theme.subBg
        }

        // 左侧亮黄竖边。
        // 必须用 ZSkewRect 且 skew / 高度与底板完全一致 —— 这样两条左斜边
        // 是同一条线，天然贴齐边框；用直边矩形会在顶部戳出底板之外。
        ZSkewRect {
            x: 0
            y: 0
            width: 5 + control.skewAmount
            height: parent.height
            skew: control.skewAmount
            color: Theme.accent
        }
    }

    // 文字：contentItem 用 Text，宽度由自己算，不会再和背景各算一套
    contentItem: Text {
        text: control.text
        font: Theme.fontMonoSm
        color: Theme.textOnSub
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
