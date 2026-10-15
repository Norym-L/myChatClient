import QtQuick
import QtQuick.Layouts
import ZmdUI 1.0

import MyChat.Core 1.0
import "../components"

/*!
    聊天主页 —— 左侧导航（会话 / 联系人 / 好友申请）+ 右侧内容区。

    ⚠ 能力边界（取决于服务端，不是客户端偷懒）：
      · **联系人**（搜索用户 / 发好友申请）服务端已实现 → 可用
      · **好友申请列表** 服务端已实现 → 可用
      · **会话列表 / 消息气泡** 服务端**没有实现**（好友列表是空函数，
        文本聊天消息完全没有相关代码）→ 本页只放说明，等服务端补齐后再接

    网络与协议全部交给 ContactController，本页不含任何协议细节。
*/
Item {
    id: page

    /*! 退出登录 / 返回登录页 */
    signal logoutRequested()

    /// 当前分区：0 会话 / 1 联系人 / 2 好友申请
    property int section: 1

    Component.onCompleted: {
        ContactController.refreshApplyList()

        // 自动化验证：MYCHAT_SEARCH 注入的关键字 —— 切到联系人分区发起搜索。
        // ⚠ refreshApplyList 已把控制器置 busy，直接 searchUser 会被静默丢弃
        //   （和自检程序踩过的同一个竞态），所以先挂起，busy 空闲后再补发。
        if (app.autoSearch !== "") {
            page.section = 1
            keywordEdit.text = app.autoSearch
            pendingSearch = app.autoSearch
            tryAutoSearch()
        }
    }

    /// MYCHAT_SEARCH 挂起的搜索关键字；空串表示没有待发的搜索
    property string pendingSearch: ""

    function tryAutoSearch() {
        if (pendingSearch === "")
            return
        if (ContactController.busy)
            return // 忙碌中，等 busyChanged 再试
        const kw = pendingSearch
        pendingSearch = ""
        ContactController.searchUser(kw)
    }

    // ── 工程图纸网格铺底 ──────────────────────────────────────
    ZGridBackground {
        anchors.fill: parent
        bgColor: Theme.bg
        minorColor: Theme.gridLine
        majorColor: Theme.gridLineMajor
        crossColor: Theme.lineSoft
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ── 左侧导航 ─────────────────────────────────────────
        ZSideBar {
            id: sidebar
            Layout.fillHeight: true
            Layout.preferredWidth: 236

            brand: "MYCHAT"
            brandSub: "MESSAGE TERMINAL"
            brandCode: "CLI / 01"
            version: "BUILD 0.1"

            model: [
                {label: "会 话", badge: "!"},
                {label: "联系人", badge: ""},
                {
                    label: "好友申请", badge: ContactController.applyList.length > 0
                        ? String(ContactController.applyList.length) : ""
                }
            ]

            currentIndex: page.section
            onActivated: (index) => {
                page.section = index
            }
        }

        // ── 右侧内容区 ───────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // 顶部：当前登录身份
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 56
                color: Theme.surface

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingMd
                    spacing: Theme.spacingMd

                    ZAvatar {
                        size: 32
                        name: page.displayName()
                        uid: page.displayUid()
                        online: true
                    }

                    Text {
                        text: LoginController.userLabel
                        font: Theme.fontStrong
                        color: Theme.ink
                        Layout.fillWidth: true
                    }

                    ZButton {
                        text: "退出"
                        variant: Theme.Variant.Ghost
                        onClicked: page.logoutRequested()
                    }
                }
            }

            // 内容：按分区切换
            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // ── 0. 会话（服务端未支持）────────────────────
                ZPanel {
                    anchors.centerIn: parent
                    width: Math.min(520, parent.width - 40)
                    visible: page.section === 0

                    title: "会话"
                    eyebrow: "SESSION LIST"
                    subtitle: "等待服务端支持"
                    tag: "TODO"
                    footerText: "服务端 getUserApplyListAndFriendList 为空实现"

                    Text {
                        width: parent.width
                        text: "会话列表需要「好友列表」接口，"
                            + "服务端 ChatServer 的 getUserApplyListAndFriendList 目前是空函数（//TODO）。"
                            + "\n文本聊天消息（TextChatMsg*）服务端也没有实现。"
                            + "\n等服务端补齐后，这里会换成真实的会话列表与消息气泡。"
                        font: Theme.fontBody
                        color: Theme.muted
                        wrapMode: Text.WordWrap
                    }
                }

                // ── 1. 联系人：搜索用户 + 发好友申请 ────────────
                Column {
                    id: searchArea
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    spacing: Theme.spacingMd
                    visible: page.section === 1

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
                            onAccepted: page.doSearch()
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
                            onClicked: page.doSearch()
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
                                onClicked: searchArea.selectedUid = modelData.uid
                            }
                        }
                    }

                    // 给选中用户发申请：验证信息 + 提交
                    ZTextField {
                        id: verifyEdit
                        width: parent.width
                        visible: searchArea.selectedUid !== ""
                        label: "VERIFY INFO"
                        unit: "MSG"
                        placeholderText: "给 " + searchArea.selectedUid + " 的验证信息"
                        enabled: !ContactController.busy
                        onAccepted: page.doAddFriend()
                    }

                    ZButton {
                        visible: searchArea.selectedUid !== ""
                        text: ContactController.busy
                            ? "提交中…"
                            : "向 UID " + searchArea.selectedUid + " 添加好友"
                        variant: Theme.Variant.Primary
                        mono: "SEND"
                        enabled: !ContactController.busy
                        onClicked: page.doAddFriend()
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

                // ── 2. 好友申请列表 ───────────────────────────
                Column {
                    anchors.fill: parent
                    anchors.margins: Theme.spacingLg
                    spacing: Theme.spacingMd
                    visible: page.section === 2

                    Row {
                        width: parent.width
                        spacing: Theme.spacingMd

                        ZSectionTitle {
                            width: parent.width - refreshBtn.width - parent.spacing
                            index: "02"
                            eyebrow: "FRIEND REQUESTS"
                            title: "好友申请"
                            subtitle: "共 " + ContactController.applyList.length + " 条"
                        }

                        ZButton {
                            id: refreshBtn
                            anchors.bottom: parent.bottom
                            text: ContactController.busy ? "…" : "刷新"
                            variant: Theme.Variant.Secondary
                            enabled: !ContactController.busy
                            onClicked: ContactController.refreshApplyList()
                        }
                    }

                    Text {
                        width: parent.width
                        visible: ContactController.applyList.length === 0
                        text: "暂无好友申请"
                        font: Theme.fontBody
                        color: Theme.muted
                    }

                    // 申请列表：ZListView 的 delegate 用自定义项，
                    // 因为 ZListItem 只有 label/description/trailing，放不下头像
                    Flickable {
                        width: parent.width
                        height: parent.height - 100
                        contentHeight: applyCol.implicitHeight
                        clip: true

                        Column {
                            id: applyCol
                            width: parent.width
                            spacing: Theme.spacingSm

                            Repeater {
                                model: ContactController.applyList

                                Rectangle {
                                    width: applyCol.width
                                    height: 64
                                    color: Theme.surface
                                    border.width: 1
                                    border.color: Theme.lineSoft

                                    Row {
                                        anchors.fill: parent
                                        anchors.margins: Theme.spacingMd
                                        spacing: Theme.spacingMd

                                        ZAvatar {
                                            size: 36
                                            name: modelData.name
                                            uid: modelData.fromUid
                                        }

                                        Column {
                                            width: parent.width - 36 - parent.spacing
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 2

                                            Text {
                                                text: modelData.name + "  (UID " + modelData.fromUid + ")"
                                                font: Theme.fontStrong
                                                color: Theme.ink
                                            }
                                            Text {
                                                width: parent.width
                                                text: modelData.verifyInfo.length > 0
                                                    ? modelData.verifyInfo : "（无验证信息）"
                                                font: Theme.fontBody
                                                color: Theme.muted
                                                elide: Text.ElideRight
                                            }
                                        }
                                    }

                                    // 右侧状态：服务端 state 语义未文档化，这里原样显示
                                    Text {
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.rightMargin: Theme.spacingMd
                                        text: "STATE " + modelData.state
                                        font: Theme.fontMonoSm
                                        color: Theme.muted
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    function doSearch() {
        ContactController.searchUser(keywordEdit.text)
    }

    function doAddFriend() {
        if (searchArea.selectedUid === "")
            return
        ContactController.sendFriendRequest(searchArea.selectedUid, verifyEdit.text, "")
    }

    /// 顶部头像显示用：从 LoginController.userLabel 里拆不出 uid，
    /// 这里改由 ContactController 的搜索态之外单独取 —— 简单起见用 SessionManager 的标签兜底
    function displayName() {
        const label = LoginController.userLabel
        const idx = label.indexOf("·")
        return idx >= 0 ? label.substring(idx + 1).trim() : label
    }

    function displayUid() {
        const label = LoginController.userLabel
        const idx = label.indexOf("·")
        return idx >= 0 ? label.substring(0, idx).replace("UID", "").trim() : ""
    }

    Connections {
        target: ContactController

        function onBusyChanged() {
            page.tryAutoSearch()
        }

        function onValidationFailed(field) {
            if (field === "keyword")
                keywordEdit.invalid = true
        }
    }
}
