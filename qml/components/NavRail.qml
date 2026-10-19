import QtQuick
import ZmdUI 1.0

/*!
    左侧图标导航栏（QQ / 微信那套三段式布局）。

    list
    li 顶部：自己的头像（点击开自己的资料卡）
    li 中部：功能图标（会话 / 联系人 / 好友申请），选中态亮黄
    li 底部：辅助图标（主题 / 退出登录）
    endlist

    和 ZSideBar 的区别：ZSideBar 是"品牌 + 文字导航"的仪表盘侧栏，宽 236px；
    这一版按聊天软件的习惯收成 64px 图标条，把横向空间让给会话列表和消息区。
    配色仍是 ZmdUI 的黑色带（不跟随亮暗主题反转），亮黄只用在选中态和角标。
*/
Item {
    id: root

    /*! 当前选中的功能项（0 会话 / 1 联系人 / 2 好友申请） */
    property int currentIndex: 0
    /*! 会话总未读（会话图标角标） */
    property int chatBadge: 0
    /*! 好友申请未读数（好友申请图标角标） */
    property int applyBadge: 0
    /*! 自己的昵称 / uid（头像用） */
    property string selfName: ""
    property string selfUid: ""
    /*! 自己是否在线（头像右下角状态点） */
    property bool selfOnline: true

    /*! 点了某个功能项 */
    signal activated(int index)
    /*! 点了自己的头像 */
    signal selfClicked()
    /*! 点了主题按钮 */
    signal themeClicked()
    /*! 点了退出按钮 */
    signal logoutClicked()

    implicitWidth: 64
    implicitHeight: 520

    // 侧边栏属于"深灰 / 纯黑"那一档，不跟随亮暗主题反转
    // （和 ZSideBar 的 panelColor 完全一致，切主题时不至于闪一下）
    readonly property color panelColor: Theme.isDark ? "#0C0C0C" : "#101010"
    readonly property color idleIcon: "#8C8C8C"

    Rectangle {
        anchors.fill: parent
        color: root.panelColor
    }

    // 右边缘分隔线
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: "#000000"
        opacity: 0.85
    }

    // ── 顶部：自己 ────────────────────────────────────────
    Item {
        id: selfBox
        anchors.top: parent.top
        anchors.topMargin: 14
        anchors.horizontalCenter: parent.horizontalCenter
        width: 44
        height: 38

        ZAvatar {
            anchors.centerIn: parent
            size: 34
            name: root.selfName
            uid: root.selfUid
            online: root.selfOnline
            clickable: true
            onClicked: root.selfClicked()
        }

        HoverHandler {
            id: selfHover
            cursorShape: Qt.PointingHandCursor
        }

        ZToolTip {
            text: root.selfName.length > 0 ? ("我的资料 · " + root.selfName) : "我的资料"
            visible: selfHover.hovered
        }
    }

    // ── 中部：功能图标 ────────────────────────────────────
    Column {
        id: navCol
        anchors.horizontalCenter: parent.horizontalCenter
        // 紧跟在头像下方，而不是整体垂直居中：QQ / 微信的图标都是贴着头像的，
        // 居中的话上半截会空出一大片。
        anchors.top: selfBox.bottom
        anchors.topMargin: 14
        spacing: 8

        NavRailButton {
            kind: "chat"
            tip: "会话"
            active: root.currentIndex === 0
            badge: root.chatBadge
            idleColor: root.idleIcon
            onClicked: root.activated(0)
        }

        NavRailButton {
            kind: "contacts"
            tip: "联系人"
            active: root.currentIndex === 1
            idleColor: root.idleIcon
            onClicked: root.activated(1)
        }

        NavRailButton {
            kind: "apply"
            tip: "好友申请"
            active: root.currentIndex === 2
            badge: root.applyBadge
            idleColor: root.idleIcon
            onClicked: root.activated(2)
        }
    }

    // ── 底部：辅助图标 ────────────────────────────────────
    Column {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        spacing: 4

        NavRailButton {
            kind: "theme"
            tip: "外观 / 主题"
            width: 40
            height: 38
            idleColor: root.idleIcon
            onClicked: root.themeClicked()
        }

        NavRailButton {
            kind: "logout"
            tip: "退出登录"
            width: 40
            height: 38
            idleColor: root.idleIcon
            onClicked: root.logoutClicked()
        }
    }
}
