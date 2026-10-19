import QtQuick
import ZmdUI 1.0

/*!
    左侧图标导航栏里的一个按钮：图标 + 选中态 + 角标 + 悬停提示。

    纯展示组件 —— 点下去干什么由页面决定（这里只发 \l clicked）。
    选中态沿用会话列表那套语汇：亮黄底 + 左侧黑条 + 黑图标。
*/
Item {
    id: root

    /*! 图标类别，见 NavIcon.kind */
    property string kind: "chat"
    /*! 悬停提示文案（空串则不弹） */
    property string tip: ""
    /*! 是否当前选中 */
    property bool active: false
    /*! 右上角角标数字，0 不显示 */
    property int badge: 0

    /*! 未选中时的图标色（深色带上用灰） */
    property color idleColor: "#8C8C8C"
    /*! 悬停时的图标色 */
    property color hoverColor: "#FFFFFF"
    /*! 选中时的图标色 */
    property color activeColor: "#0C0C0C"
    /*! 悬停底色（深色带上的浅一档黑） */
    property color hoverFill: "#1F1F1F"

    signal clicked()

    implicitWidth: 44
    implicitHeight: 40

    // ── 底色：选中亮黄 / 悬停深灰 / 其余透明 ──
    Rectangle {
        anchors.fill: parent
        color: root.active ? Theme.accent
            : pointer.hovered ? root.hoverFill
                : "transparent"

        Behavior on color {
            ColorAnimation {
                duration: Theme.durFast; easing.type: Theme.easingStd
            }
        }
    }

    // ── 选中指示：左侧黑条（与会话行的选中指示同一套）──
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 2
        visible: root.active
        color: Theme.inkBlock
    }

    NavIcon {
        anchors.centerIn: parent
        width: 20
        height: 20
        kind: root.kind
        color: root.active ? root.activeColor
            : pointer.hovered ? root.hoverColor
                : root.idleColor
    }

    // ── 未读角标：亮黄小方块，贴在右上角 ──
    Rectangle {
        id: badgeBox
        visible: root.badge > 0
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: -5
        anchors.topMargin: -4
        width: Math.max(14, badgeText.implicitWidth + 7)
        height: 14
        color: Theme.accent
        // 用导航栏底色描边，把角标从图标底色里"切"出来
        border.width: 1
        border.color: Theme.isDark ? "#0C0C0C" : "#101010"

        Text {
            id: badgeText
            anchors.centerIn: parent
            text: root.badge > 99 ? "99+" : String(root.badge)
            // 角标只有 14px 高，等宽 11px 会顶出去 —— 单独给一个更小的字号
            font: Qt.font({
                family: Theme.familyMono,
                pixelSize: 9
            })
            color: Theme.textOnAccent
        }
    }

    HoverHandler {
        id: pointer
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: root.clicked()
    }

    ZToolTip {
        text: root.tip
        visible: root.tip.length > 0 && pointer.hovered
    }
}
