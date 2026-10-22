import QtQuick
import ZmdUI 1.0

import MyChat.Core 1.0
import "../../components"

/*!
    联系人分区（左侧导航第 2 项）：中栏好友列表 + 右栏「找人」。

    两栏都是只读展示：点好友一行 → 抛信号，资料卡（发消息 / 删除好友）由 ChatPage
    统一打开；搜索走 ContactController.searchUser，命中多条时由用户点选，
    真正的「加好友」动作只在资料卡里做，避免同一页出现两套流程。
*/

Row {
    id: pane

    /// 请求打开某个用户的资料卡（好友 / 搜索结果都走这里）
    signal userInfoRequested(var user)

    /// 自动化 / 外部注入搜索关键字时挂起的那次搜索（控制器忙时会被静默丢弃）
    property string pendingSearch: ""

    /*!
        用指定关键字发起搜索。

        \note 不能直接调 searchUser：进页面那一刻 refreshApplyList 往往正占着
              控制器，忙的时候发请求会被**静默丢弃**（自检程序踩过的同一个竞态）。
              所以先挂起，等 busyChanged 落回 false 再补发。
    */
    function searchWith(keyword) {
        keywordEdit.text = keyword
        pane.pendingSearch = keyword
        pane.tryPendingSearch()
    }

    function tryPendingSearch() {
        if (pane.pendingSearch.length === 0 || ContactController.busy)
            return
        const kw = pane.pendingSearch
        pane.pendingSearch = ""
        ContactController.searchUser(kw)
    }

    /// 搜索当前关键字（搜索框回车 / 点「搜索」都走这里）
    function doSearch() {
        pane.pendingSearch = ""
        ContactController.searchUser(keywordEdit.text)
    }

    spacing: 0
    anchors.fill: parent

    /// 关键词过滤后的好友（数据源 ContactController.friendList）
    property var visibleFriends: []

    function reloadFriends() {
        const all = ContactController.friendList
        const kw = friendSearch.text.trim().toLowerCase()
        if (kw.length === 0) {
            visibleFriends = all
            return
        }

        const out = []
        for (let i = 0; i < all.length; ++i) {
            const f = all[i]
            if (String(f.name).toLowerCase().indexOf(kw) >= 0
                || String(f.displayName).toLowerCase().indexOf(kw) >= 0
                || String(f.uid).indexOf(kw) >= 0)
                out.push(f)
        }
        visibleFriends = out
    }

    Component.onCompleted: reloadFriends()

    Connections {
        target: ContactController

        function onFriendListChanged() {
            pane.reloadFriends()
        }

        function onBusyChanged() {
            pane.tryPendingSearch()
        }

        // 关键字校验失败（空串 / 非法字符）→ 搜索框标红
        function onValidationFailed(field) {
            if (field === "keyword")
                keywordEdit.invalid = true
        }
    }

    // ══════ 中：好友列表 ══════
    Rectangle {
        width: 244
        height: parent.height
        color: Theme.surface

        // 右缘分隔线：和会话列表同一条视觉语言
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

            Item {
                id: contactsHeader
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 42

                Text {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    text: "好友"
                    font: Theme.fontHeading
                    color: Theme.ink
                }

                Text {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.topMargin: 30
                    text: "CONTACTS · " + ContactController.friendCount + " 位"
                    font: Theme.fontMonoSm
                    color: Theme.muted
                    opacity: 0.8
                }

                ZIconButton {
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: "⟳"
                    tooltip: ContactController.busy
                        ? "正在刷新…" : "刷新好友列表"
                    enabled: !ContactController.busy
                    onClicked: ContactController.refreshFriendList()
                }
            }

            ZTextField {
                id: friendSearch
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: contactsHeader.bottom
                anchors.topMargin: Theme.spacingSm
                placeholderText: "搜索好友"
                onTextChanged: pane.reloadFriends()
            }

            Item {
                id: friendArea
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: friendSearch.bottom
                anchors.topMargin: Theme.spacingSm
                anchors.bottom: parent.bottom

                Flickable {
                    anchors.fill: parent
                    clip: true
                    contentHeight: friendColumn.height
                    boundsBehavior: Flickable.StopAtBounds

                    Column {
                        id: friendColumn
                        width: parent.width
                        spacing: 0

                        Repeater {
                            model: pane.visibleFriends

                            ConversationRow {
                                required property var modelData

                                width: friendColumn.width
                                uid: String(modelData.uid)
                                // 备注优先；字段缺失时要挡一下，
                                // 否则 String(undefined) 会显示成 "undefined"
                                name: (modelData.displayName !== undefined
                                    && String(modelData.displayName).length > 0)
                                    ? String(modelData.displayName)
                                    : String(modelData.name)
                                lastText: (modelData.desc !== undefined
                                    && String(modelData.desc).length > 0)
                                    ? String(modelData.desc) : "（暂无签名）"
                                lastTime: ""
                                unread: 0
                                selected: false
                                online: PresenceController.online[String(modelData.uid)] === true
                                onClicked: pane.userInfoRequested(modelData)
                            }
                        }
                    }
                }

                Text {
                    anchors.centerIn: parent
                    visible: pane.visibleFriends.length === 0
                    text: friendSearch.text.length > 0
                        ? "没有匹配的好友"
                        : (ContactController.busy
                            ? "正在获取好友列表…" : "暂无好友")
                    font: Theme.fontBody
                    color: Theme.muted
                }
            }
        }
    }

    // ══════ 右：搜索用户 ══════
    Item {
        width: parent.width - 244
        height: parent.height

        Column {
            id: searchArea
            anchors.fill: parent
            anchors.margins: Theme.spacingLg
            spacing: Theme.spacingMd

    ZSectionTitle {
        width: parent.width
        index: "01"
        eyebrow: "FIND USER"
        title: "搜索用户"
        subtitle: "支持按 uid 或用户名搜索"
    }

    // 搜索行
    Row {
        width: parent.width
        spacing: Theme.spacingSm

        ZTextField {
            id: keywordEdit
            width: parent.width - searchBtn.width - parent.spacing
            label: "KEYWORD"
            unit: "UID/NAME"
            placeholderText: "输入 uid 或用户名"
            enabled: !ContactController.busy
            onAccepted: pane.doSearch()
            onTextChanged: {
                invalid = false
                searchArea.selectedUid = ""
                ContactController.clearSearchResult()
            }
        }

        ZButton {
            id: searchBtn
            anchors.bottom: keywordEdit.bottom
            text: ContactController.busy ? "…" : "搜索"
            variant: Theme.Variant.Secondary
            enabled: !ContactController.busy
            onClicked: pane.doSearch()
        }
    }

    // 搜索结果列表：服务端可能命中多个（同名/相似用户），点选一条再发申请。
    // ⚠ 选中态挂在 id（searchArea）上：QML 子对象看不到父对象的裸属性名，
    //   裸写 selectedUid 会 ReferenceError，必须全部 searchArea.selectedUid。
    property string selectedUid: ""

    Repeater {
        model: ContactController.searchResults

        delegate: Rectangle {
            required property var modelData

            width: parent.width
            height: 76
            color: searchArea.selectedUid === modelData.uid ? Theme.accentSoft
                : Theme.surface
            border.width: searchArea.selectedUid === modelData.uid ? 2 : 1
            border.color: searchArea.selectedUid === modelData.uid ? Theme.accent
                : Theme.lineSoft

            Row {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
                spacing: Theme.spacingMd

                ZAvatar {
                    size: 48
                    name: modelData.name
                    uid: modelData.uid
                    // 查到了才点亮；未知不点亮（和弹窗一个规矩）
                    online: PresenceController.online[modelData.uid] === true
                }

                Column {
                    width: parent.width - 48 - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 2

                    Row {
                        spacing: Theme.spacingSm

                        Text {
                            id: resultName
                            text: modelData.name
                            font: Theme.fontStrong
                            color: Theme.ink
                        }

                        Text {
                            text: "UID " + modelData.uid
                            font: Theme.fontMonoSm
                            color: Theme.muted
                            anchors.baseline: resultName.baseline
                        }
                    }

                    Text {
                        width: parent.width
                        text: modelData.desc.length > 0
                            ? modelData.desc : "（个性签名为空）"
                        font: Theme.fontBody
                        color: Theme.muted
                        elide: Text.ElideRight
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                // 点整行 = 选中 + 打开资料卡（加好友在资料卡里做）。
                // ⚠ 只留这一个处理器：头像上再挂一个 TapHandler 的话，
                //   两个都会响应，弹窗会被连着 open 两次。
                onClicked: {
                    searchArea.selectedUid = modelData.uid
                    pane.userInfoRequested(modelData)
                }
            }
        }
    }

    // 加好友的入口统一收进资料弹窗（点整行即打开）：
    // 验证信息、网络结果提示都在那儿，避免同一页出现两套"加好友"流程。
    Text {
        width: parent.width
        visible: searchArea.selectedUid !== ""
        text: "已选中 UID " + searchArea.selectedUid
            + " —— 在资料卡里填写验证信息后发送好友申请"
        font: Theme.fontBody
        color: Theme.muted
        wrapMode: Text.WordWrap
    }

    // 提示行
    Text {
        width: parent.width
        visible: ContactController.tip.length > 0
        text: ContactController.tip
        font: Theme.fontBody
        color: ContactController.tipIsError ? Theme.danger : Theme.muted
        wrapMode: Text.WordWrap
    }
        }
    }
}
