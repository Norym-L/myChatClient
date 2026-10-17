import QtQuick
import ZmdUI 1.0

/*!
    会话列表的一行：头像 + 昵称/时间 + 摘要/未读角标。

    纯展示组件 —— 点哪一行、点完干什么由页面决定（这里只发 \l clicked）。
    选中态沿用侧边栏那套：亮黄底 + 黑字，左边的竖条是"当前会话"的指示。
*/
Item {
    id: root

    /*! 对方的 uid（同时用来生成头像底色） */
    property string uid: ""
    /*! 对方昵称 */
    property string name: ""
    /*! 最后一条消息的摘要 */
    property string lastText: ""
    /*! 最后一条消息的时间（原样显示，格式由控制器给） */
    property string lastTime: ""
    /*! 未读数，0 不显示角标 */
    property int unread: 0
    /*! 是否当前选中 */
    property bool selected: false
    /*! 是否在线（未知时传 false —— 未知不等于离线，别点亮） */
    property bool online: false

    signal clicked()

    implicitHeight: 62
    width: parent ? parent.width : 240

    // ── 底色：选中亮黄，悬停浅灰，其余透明（列表本身是白卡片）──
    Rectangle {
        anchors.fill: parent
        color: root.selected ? Theme.accent
            : hover.hovered ? Theme.surfaceHover
                : "transparent"

        Behavior on color {
            ColorAnimation {
                duration: Theme.durFast; easing.type: Theme.easingStd
            }
        }
    }

    // ── 选中指示条 ──
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: root.selected ? 4 : 0
        color: Theme.inkBlock
    }

    // ── 分隔线（最后一行不画，交给页面判断不好办，这里统一画在底部，选中时隐藏）──
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.selected ? 4 : 0
        height: 1
        color: Theme.lineSoft
        opacity: root.selected ? 0 : 0.6
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingMd + 2
        anchors.rightMargin: Theme.spacingMd
        anchors.topMargin: Theme.spacingSm
        anchors.bottomMargin: Theme.spacingSm
        spacing: Theme.spacingSm

        ZAvatar {
            anchors.verticalCenter: parent.verticalCenter
            size: 36
            name: root.name
            uid: root.uid
            online: root.online
        }

        Item {
            width: parent.width - 36 - parent.spacing
            height: parent.height

            // 昵称 + 时间
            Item {
                id: titleRow
                anchors.top: parent.top
                width: parent.width
                height: 20

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - timeText.width - Theme.spacingSm
                    text: root.name.length > 0 ? root.name : root.uid
                    font: Theme.fontStrong
                    color: root.selected ? Theme.textOnAccent : Theme.ink
                    elide: Text.ElideRight
                }

                Text {
                    id: timeText
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.lastTime
                    font: Theme.fontMonoSm
                    color: root.selected ? Theme.textOnAccent : Theme.muted
                    opacity: root.selected ? 0.7 : 1
                }
            }

            // 摘要 + 未读
            Item {
                anchors.top: titleRow.bottom
                anchors.topMargin: 4
                width: parent.width
                height: 18

                Text {
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - (badge.visible ? badge.width + Theme.spacingSm : 0)
                    text: root.lastText.length > 0 ? root.lastText : "（暂无消息）"
                    font: Theme.fontBody
                    color: root.selected ? Theme.textOnAccent : Theme.muted
                    opacity: root.selected ? 0.75 : 1
                    elide: Text.ElideRight
                }

                // 未读角标：亮黄在亮黄底上看不见 → 选中时反相成黑底
                Rectangle {
                    id: badge
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.unread > 0
                    width: Math.max(18, badgeText.implicitWidth + 10)
                    height: 16
                    color: root.selected ? Theme.inkBlock : Theme.accent

                    Text {
                        id: badgeText
                        anchors.centerIn: parent
                        text: root.unread > 99 ? "99+" : String(root.unread)
                        font: Theme.fontMonoSm
                        color: root.selected ? Theme.textOnSub : Theme.textOnAccent
                    }
                }
            }
        }
    }

    HoverHandler {
        id: hover
        cursorShape: Qt.PointingHandCursor
    }

    TapHandler {
        onTapped: root.clicked()
    }
}
