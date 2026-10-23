import QtQuick
import ZmdUI 1.0

/*!
    好友多选器（建群 / 拉人共用）。

    纯展示组件：好友列表由页面喂进来（ContactController.friendList），
    选中集合也由页面持有（\l selectedUids，`uid -> true` 的对象）。
    点一行只发 \l toggled(uid)，**不自己改选中状态** ——
    QML 里对 JS 对象属性的写入不会触发绑定更新，必须整体替换，
    这件事交给持有方做才不会出现"勾了但框没变"。

    \note 选中集合用"整体替换"的写法（页面里 `pickerSel = Object.assign({}, ...)`），
          和 ChatPane 的 selectedIds 同一套约定。
*/

Item {
    id: root

    /// 好友列表，每项 QVariantMap：uid / displayName / name / icon
    property var friends: []
    /// 选中集合：uid -> true
    property var selectedUids: ({})
    /// 最多能选几个；0 = 不限
    property int maxSelected: 0
    /// 没有好友时的提示
    property string emptyText: "还没有好友 —— 先到「联系人」里加一个"

    signal toggled(string uid)

    /// 过滤后的好友（搜索框一变就重算）
    property var visibleFriends: []

    readonly property int selectedCount: {
        let count = 0
        for (const key in selectedUids) {
            if (selectedUids[key] === true)
                ++count
        }
        return count
    }

    function reload() {
        const kw = pickSearch.text.trim().toLowerCase()
        const all = root.friends
        if (kw.length === 0) {
            visibleFriends = all
            return
        }

        const out = []
        for (let i = 0; i < all.length; ++i) {
            const f = all[i]
            const name = String(f.displayName !== undefined ? f.displayName : f.name)
            if (name.toLowerCase().indexOf(kw) >= 0
                    || String(f.uid).indexOf(kw) >= 0)
                out.push(f)
        }
        visibleFriends = out
    }

    Component.onCompleted: reload()
    onFriendsChanged: reload()

    implicitHeight: 300

    Column {
        anchors.fill: parent
        spacing: Theme.spacingSm

        Item {
            width: parent.width
            height: 34

            ZTextField {
                id: pickSearch
                anchors.left: parent.left
                anchors.right: countText.left
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                placeholderText: "搜索好友"
                onTextChanged: root.reload()
            }

            Text {
                id: countText
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: root.maxSelected > 0
                      ? (root.selectedCount + " / " + root.maxSelected)
                      : ("已选 " + root.selectedCount)
                font: Theme.fontMonoSm
                color: Theme.muted
            }
        }

        Rectangle {
            width: parent.width
            height: parent.height - 34 - Theme.spacingSm
            color: Theme.surface
            border.color: Theme.line
            border.width: Theme.border

            Flickable {
                id: pickFlick
                anchors.fill: parent
                anchors.margins: 1
                clip: true
                contentHeight: pickColumn.height
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: pickColumn
                    width: pickFlick.width
                    spacing: 0

                    Repeater {
                        model: root.visibleFriends

                        Item {
                            id: rowItem
                            required property var modelData
                            width: pickColumn.width
                            height: 48

                            readonly property string rowUid: String(modelData.uid)
                            readonly property bool checked:
                                root.selectedUids[rowUid] === true
                            readonly property string rowName: {
                                const display = modelData.displayName
                                if (display !== undefined && String(display).length > 0)
                                    return String(display)
                                return String(modelData.name !== undefined ? modelData.name : rowUid)
                            }

                            Rectangle {
                                anchors.fill: parent
                                color: rowHover.hovered ? Theme.surfaceHover : "transparent"
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: Theme.lineSoft
                                opacity: 0.6
                            }

                            // 勾选框：自绘（ZmdUI 没有 checkbox，形状也更好控制）
                            Rectangle {
                                id: box
                                anchors.left: parent.left
                                anchors.leftMargin: Theme.spacingSm
                                anchors.verticalCenter: parent.verticalCenter
                                width: 16
                                height: 16
                                color: rowItem.checked ? Theme.accent : "transparent"
                                border.color: Theme.ink
                                border.width: Theme.border

                                Text {
                                    anchors.centerIn: parent
                                    visible: rowItem.checked
                                    text: "✓"
                                    font: Theme.fontMonoSm
                                    color: Theme.textOnAccent
                                }
                            }

                            ZAvatar {
                                id: pickAvatar
                                anchors.left: box.right
                                anchors.leftMargin: Theme.spacingSm
                                anchors.verticalCenter: parent.verticalCenter
                                size: 28
                                name: rowItem.rowName
                                uid: rowItem.rowUid
                            }

                            Text {
                                anchors.left: pickAvatar.right
                                anchors.leftMargin: Theme.spacingSm
                                anchors.right: parent.right
                                anchors.rightMargin: Theme.spacingSm
                                anchors.verticalCenter: parent.verticalCenter
                                text: rowItem.rowName
                                font: Theme.fontBody
                                color: Theme.ink
                                elide: Text.ElideRight
                            }

                            HoverHandler {
                                id: rowHover
                                cursorShape: Qt.PointingHandCursor
                            }

                            TapHandler {
                                onTapped: root.toggled(rowItem.rowUid)
                            }
                        }
                    }

                    Text {
                        width: pickColumn.width
                        height: 60
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        visible: root.visibleFriends.length === 0
                        text: pickSearch.text.length > 0
                              ? "没有匹配的好友"
                              : root.emptyText
                        font: Theme.fontBody
                        color: Theme.muted
                    }
                }
            }
        }
    }
}
