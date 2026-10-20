import QtQuick
import ZmdUI 1.0

import "."

/*!
    登录页的账号卡片（账号条里的一张）。

    形态：左侧头像 + 昵称 / uid，右上角「上次」标记，选中时左侧亮一条强调色边。

    为什么单独一个组件：LoginPage 已经 146 行，账号条自己就有选中态、悬停态、
    删除按钮三套状态，塞回页面里会把它撑成第二个 ChatPage。这里只做展示，
    点击 / 双击 / 删除全部通过 signal 抛出去 —— 页面决定选谁、进哪儿。
*/
Rectangle {
    id: root

    /*! 账号 uid（字符串，避免大 uid 在 JS 里丢精度） */
    property string uid: ""
    /*! 昵称；空时用 uid 兜底显示 */
    property string name: ""
    /*! 有没有免密凭据：有的话卡片右下角显示「免密」 */
    property bool hasToken: false
    /*! 是不是上次用的账号（打「上次」标记） */
    property bool isLast: false
    /*! 上次登录时间（"10-20 23:31"），空串则不显示 */
    property string lastText: ""
    /*! 当前是否选中 */
    property bool selected: false

    /*! 单击：选中这张卡 */
    signal clicked()
    /*! 双击：直接免密进入（没有凭据时页面会忽略） */
    signal activated()
    /*! 点右上角 ×：从本机移除这个账号 */
    signal removeRequested()

    width: 132
    height: 88

    color: mouse.containsMouse ? Theme.surfaceHover : Theme.surfaceMuted
    border.width: 1
    border.color: selected ? Theme.accent : Theme.lineSoft
    // 工业风：切一个小角，和 ZCutRect / ZPanel 的斜切语言一致
    radius: 0

    // 选中时左侧亮一条：比整框变色更克制，一眼能看出"现在选的是这张"
    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 3
        color: Theme.accent
        visible: root.selected
    }

    ZAvatar {
        id: avatar
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSm + 2
        anchors.top: parent.top
        anchors.topMargin: Theme.spacingSm
        size: 28
        name: root.name.length > 0 ? root.name : root.uid
        uid: root.uid
    }

    Text {
        anchors.left: avatar.right
        anchors.leftMargin: Theme.spacingSm
        anchors.right: parent.right
        // 右上角有「上次」标记时给它让位，否则昵称会被压在标记底下
        anchors.rightMargin: root.isLast ? lastBadge.width + 6 : Theme.spacingSm
        anchors.top: avatar.top
        height: avatar.height
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        text: root.name.length > 0 ? root.name : root.uid
        font: Theme.fontBody
        color: Theme.ink
    }

    Text {
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSm + 2
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSm
        anchors.top: avatar.bottom
        anchors.topMargin: 2
        elide: Text.ElideRight
        text: "UID " + root.uid
        font: Theme.fontMonoSm
        color: Theme.muted
    }

    // 底部一行：上次登录时间 + 免密标记
    Text {
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacingSm + 2
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingSm
        text: root.lastText.length > 0 ? root.lastText : "未登录过"
        font: Theme.fontMonoSm
        color: Theme.muted
    }

    // 右下角：没选中时显示"免密"状态，选中时换成"移除"动作
    // （两个都放这里会叠字，所以是互斥的两个元素）
    Text {
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSm
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingSm
        visible: root.hasToken && !root.selected
        text: "免密"
        font: Theme.fontMonoSm
        color: Theme.accent
    }

    Text {
        id: removeLabel
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacingSm
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacingSm
        visible: root.selected
        // 必须抬到整卡 MouseArea 之上，否则点「移除」会先被下面那层"选中"吃掉
        z: 2
        text: "移除"
        font: Theme.fontMonoSm
        color: removeMouse.containsMouse ? Theme.danger : Theme.muted

        MouseArea {
            id: removeMouse
            anchors.fill: parent
            // 文字本身很小，把热区往四周撑一点，不然很难点中
            anchors.margins: -4
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.removeRequested()
        }
    }

    // 「上次」标记：压在右上角，和「免密」错开
    Rectangle {
        id: lastBadge
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: 4
        width: lastLabel.width + 8
        height: lastLabel.height + 2
        visible: root.isLast
        color: Theme.accent

        Text {
            id: lastLabel
            anchors.centerIn: parent
            text: "上次"
            font: Theme.fontMonoSm
            color: Theme.textOnAccent
        }
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        // 「移除」压在右下角，它是后声明的子项，会先吃到点击
        acceptedButtons: Qt.LeftButton
        onClicked: root.clicked()
        onDoubleClicked: root.activated()
    }
}
