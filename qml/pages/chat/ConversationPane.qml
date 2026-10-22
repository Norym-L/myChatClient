import QtQuick
import ZmdUI 1.0

import MyChat.Core 1.0
import "../../components"

/*!
    会话列表（会话分区左栏，固定 244px）。

    数据源是 ChatController.conversations —— 会话本身由好友列表驱动，本组件
    只负责「过滤 + 摆出来 + 未读角标」，不发任何网络请求。

    \note 过滤结果用普通 property 而不是绑定：Repeater 的 model 需要
          "一变就重建"，而过滤是 JS 循环算出来的，得自己 reload 后再赋值。
*/

Rectangle {
    id: pane

    /// 请求切到「联系人」分区（搜索框右边的「+」）
    signal addFriendRequested()

    /// 全部会话的未读之和（左侧导航条的角标用）
    readonly property int unreadTotal: {
        let sum = 0
        const list = ChatController.conversations
        for (let i = 0; i < list.length; ++i)
            sum += Number(list[i].unread) || 0
        return sum
    }

    /// 列表里有没有演示会话（只有 MYCHAT_DEMO=1 时会有）—— 决定底部标注写法
    readonly property bool hasDemoConversation: {
        const list = ChatController.conversations
        for (let i = 0; i < list.length; ++i) {
            if (list[i].demo === true)
                return true
        }
        return false
    }

    /// 搜索框过滤后的会话
    property var visibleConversations: []

    function reloadConversations() {
        const all = ChatController.conversations
        const kw = convSearch.text.trim().toLowerCase()
        if (kw.length === 0) {
            visibleConversations = all
            return
        }

        const out = []
        for (let i = 0; i < all.length; ++i) {
            const c = all[i]
            // displayName = 备注优先（服务端 nick）；没备注时就是昵称
            if (String(c.displayName).toLowerCase().indexOf(kw) >= 0
                || String(c.name).toLowerCase().indexOf(kw) >= 0
                || String(c.uid).indexOf(kw) >= 0
                || String(c.lastText).toLowerCase().indexOf(kw) >= 0)
                out.push(c)
        }
        visibleConversations = out
    }

    Component.onCompleted: pane.reloadConversations()

    Connections {
        target: ChatController

        // 会话列表由控制器驱动（新消息 / 好友同步都会改它）→ 过滤结果跟着重算
        function onConversationsChanged() {
            pane.reloadConversations()
        }
    }

    width: 244
    height: parent.height
    color: Theme.surface

    // 右侧分隔线（列表是白卡片，靠这条线和消息区隔开）
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.line
    }

    Item {
        anchors.fill: parent
        anchors.margins: Theme.spacingMd

        /*
            头：自己的身份（QQ 把头像和昵称放在列表顶部）。

            头像在左边导航条里占了一格，这里只补文字：昵称 + UID。
            原来这行是 "SESSIONS · 2" 和刷新按钮 —— 去掉顶栏那会儿
            把用户名一起删了，界面上就再看不到"我是谁"，所以要补回来。
        */
        Item {
            id: listHeader
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: 42

            Text {
                id: listTitle
                anchors.left: parent.left
                anchors.right: friendRefresh.left
                anchors.rightMargin: Theme.spacingSm
                anchors.top: parent.top
                elide: Text.ElideRight
                text: LoginController.name.length > 0
                    ? LoginController.name
                    : LoginController.userLabel
                font: Theme.fontHeading
                color: Theme.ink
            }

            Text {
                id: listSub
                anchors.left: parent.left
                anchors.right: friendRefresh.left
                anchors.rightMargin: Theme.spacingSm
                anchors.top: listTitle.bottom
                anchors.topMargin: 2
                elide: Text.ElideRight
                text: "UID " + LoginController.uid + "  ·  "
                    + ChatController.conversationCount + " 个会话"
                font: Theme.fontMonoSm
                color: Theme.muted
                opacity: 0.8
            }

            // 好友列表是手动可刷的（进页面自动拉一次，之后靠这个按钮）
            ZIconButton {
                id: friendRefresh
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                glyph: "⟳"
                tooltip: ContactController.busy ? "正在刷新…"
                    : "刷新好友列表"
                enabled: !ContactController.busy
                onClicked: ContactController.refreshFriendList()
            }
        }

        /*
            搜索行：搜索框吃掉剩余宽度，右边跟一个固定宽度的「+」。

            「+」的语义是"找个人开聊" —— 直接跳到联系人分区，
            搜索用户 → 资料卡 → 加好友，这是唯一能新增会话的入口。
            （好友列表的刷新按钮还在上面那行标题里，不重复占位。）
        */
        Row {
            id: searchRow
            anchors.top: listHeader.bottom
            anchors.topMargin: Theme.spacingSm
            width: parent.width
            spacing: Theme.spacingSm

            ZTextField {
                id: convSearch
                width: parent.width - searchAdd.width - parent.spacing
                placeholderText: "搜索"
                onTextChanged: pane.reloadConversations()
            }

            ZIconButton {
                id: searchAdd
                glyph: "+"
                tooltip: "加好友 / 发起会话"
                onClicked: pane.addFriendRequested()
            }
        }

        Item {
            id: listArea
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: searchRow.bottom
            anchors.topMargin: Theme.spacingSm
            anchors.bottom: listNote.top
            anchors.bottomMargin: Theme.spacingSm

            Flickable {
                id: convFlick
                anchors.fill: parent
                clip: true
                contentHeight: convColumn.height
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: convColumn
                    width: convFlick.width
                    spacing: 0

                    Repeater {
                        model: pane.visibleConversations

                        ConversationRow {
                            required property var modelData

                            width: convColumn.width
                            uid: String(modelData.uid)
                            // 备注优先：控制器已经把 displayName 算好了
                            name: String(modelData.displayName)
                            lastText: modelData.lastText
                            lastTime: modelData.lastTime
                            unread: Number(modelData.unread) || 0
                            selected: ChatController.activePeerUid === String(modelData.uid)
                            // 未知 ≠ 离线：没查到状态就不点亮
                            online: PresenceController.online[String(modelData.uid)] === true
                            onClicked: ChatController.openConversation(String(modelData.uid),
                                String(modelData.displayName))
                        }
                    }
                }
            }

            Text {
                anchors.centerIn: parent
                visible: pane.visibleConversations.length === 0
                text: convSearch.text.length > 0
                    ? "没有匹配的会话"
                    : (ContactController.busy ? "正在获取好友列表…"
                        : "暂无好友 —— 去「联系人」里搜索并添加")
                font: Theme.fontBody
                color: Theme.muted
            }
        }

        /*
            数据来源如实标注：
              · 有演示会话（MYCHAT_DEMO=1）→ 明说那是本地演示数据；
              · 平时 → 会话来自好友列表，标出好友数。
        */
        Text {
            id: listNote
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            text: pane.hasDemoConversation
                ? "含 DEMO · 本地演示数据"
                : "FRIENDS · " + ContactController.friendCount + " 位好友"
            font: Theme.fontMonoSm
            color: Theme.muted
            opacity: 0.6
        }
    }
}
