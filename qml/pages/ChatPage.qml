import QtQuick
import QtQuick.Layouts
import ZmdUI 1.0

import MyChat.Core 1.0
import "../components"
import "../dialog"

/*!
    聊天主页 —— 左侧导航（会话 / 联系人 / 好友申请）+ 右侧内容区。

    ⚠ 能力边界（取决于服务端，不是客户端偷懒）：
      · **联系人**（搜索用户 / 发好友申请）服务端已实现 → 可用
      · **好友申请列表** 服务端已实现 → 可用
      · **会话列表 / 消息气泡** 服务端**没有实现**（好友列表是空函数，
        文本聊天消息完全没有相关代码）→ 本页只放说明，等服务端补齐后再接

    网络与协议全部交给 ContactController，本页不含任何协议细节。

    用户资料卡（UserInfoDialog）的两个入口：
      · 顶部自己的头像 → openSelfInfo() → 可编辑本人资料
      · 搜索结果 / 申请列表点整行 → openUserInfo() → 可发好友申请
*/
Item {
    id: page

    /*! 退出登录 / 返回登录页 */
    signal logoutRequested()

    /// 当前分区：0 会话 / 1 联系人 / 2 好友申请
    property int section: 1

    Component.onCompleted: {
        ContactController.refreshApplyList()

        // 自动化验证：MYCHAT_OPENDLG 打开资料弹窗（弹窗无头抓图抓不到，得自己弹出来）
        if (app.autoDialog !== "")
            Qt.callLater(page.openAutoDialog)

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

    /// MYCHAT_OPENDLG=sendfriend 时挂起的"替用户点一次加好友"
    property bool pendingFriendSend: false

    /// MYCHAT_OPENDLG=applyreceived / applysent 时挂起的"打开申请列表里的某一条"
    property string pendingApplyOpen: ""

    function tryAutoSearch() {
        if (pendingSearch === "")
            return
        if (ContactController.busy)
            return // 忙碌中，等 busyChanged 再试
        const kw = pendingSearch
        pendingSearch = ""
        ContactController.searchUser(kw)
    }

    /// 与 tryAutoSearch 同一个道理：控制器忙的时候发的请求会被静默丢弃
    function tryPendingFriendSend() {
        if (!pendingFriendSend || ContactController.busy)
            return
        pendingFriendSend = false
        userInfoDialog.primaryAction()
    }

    /// 同上的道理：申请列表要等回包到了才有数据
    function tryPendingApplyOpen() {
        if (pendingApplyOpen === "" || ContactController.busy)
            return

        const list = ContactController.applyList
        // ⚠ ContactController 是先 setBusy(false) 再更新列表的，所以 busyChanged 那一拍
        //   列表还是空的。这里必须等 applyListChanged —— 空列表不算"没找到"。
        if (list.length === 0)
            return

        const want = pendingApplyOpen === "applyreceived"
        for (let i = 0; i < list.length; ++i) {
            if (Boolean(list[i].received) === want) {
                pendingApplyOpen = ""
                const item = list[i]
                // 延到下一拍再开：控制器是在 emit applyListChanged **之后**才 setTip
                // 的，此刻同步开弹窗会被随后的 tipChanged 把底栏提示覆盖掉。
                // 真实用户点击本来就发生在这一切之后，所以这里只为还原真实行为。
                Qt.callLater(() => page.openUserInfo(item, item))
                return
            }
        }

        pendingApplyOpen = ""
        console.warn("[chat] 申请列表里没有「" + (want ? "收到" : "已发出") + "」的条目")
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

                    // 顶部这个就是"自己"：点它打开自己的资料卡，进去就能编辑
                    ZAvatar {
                        size: 32
                        name: LoginController.name
                        uid: LoginController.uid
                        online: true
                        clickable: true
                        onClicked: page.openSelfInfo()
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
                                    page.openUserInfo(modelData)
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
                                            // ⚠ 必须用 peerUid —— 服务端返回的列表里
                                            //   我发出的那条 fromUid 是我本人（name 却是对方的）
                                            uid: modelData.peerUid
                                            online: PresenceController.online[modelData.peerUid] === true
                                        }

                                        Column {
                                            width: parent.width - 36 - parent.spacing - direction.width
                                                - Theme.spacingMd
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 2

                                            Row {
                                                spacing: Theme.spacingSm

                                                Text {
                                                    text: modelData.name
                                                    font: Theme.fontStrong
                                                    color: Theme.ink
                                                }

                                                Text {
                                                    text: "UID " + modelData.peerUid
                                                    font: Theme.fontMonoSm
                                                    color: Theme.muted
                                                }
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

                                    // 右侧：方向 + 状态。服务端 state 语义未文档化，原样显示
                                    Row {
                                        id: direction
                                        anchors.right: parent.right
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.rightMargin: Theme.spacingMd
                                        spacing: Theme.spacingSm

                                        ZTag {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.received ? "收到" : "已发出"
                                            tone: modelData.received ? Theme.Tone.Accent
                                                : Theme.Tone.Muted
                                            uppercase: false
                                            mono: false
                                        }

                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: modelData.stateText
                                            font: Theme.fontMonoSm
                                            color: Theme.muted
                                        }
                                    }

                                    // 点整行打开申请人资料卡 —— 并带上这条申请本身，
                                    // 弹窗会切成"申请详情"形态，不再给「加好友」
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: page.openUserInfo(modelData, modelData)
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    /*
        用户资料弹窗。

        为什么由页面持有、而不是塞进 ZAvatar 里自己创建：
        UserInfoDialog 的内容里就有 ZAvatar，如果 ZAvatar 反过来再引用
        UserInfoDialog，两个文件互相依赖 → QML 报
          "Cyclic dependency detected between ZAvatar.qml and UserInfoDialog.qml"
        → 连 main.qml 都加载不了（engine.rootObjects() 为空，进程直接退出）。
        单向依赖：页面 → 弹窗 → 头像，就不会有这个问题，也顺带把"展示组件"和
        "业务弹窗"解耦了。
    */
    UserInfoDialog {
        id: userInfoDialog

        // 只读绑定：本页不会给它赋值，所以绑定不会被破坏
        busy: ContactController.busy

        // 加好友：转给 ContactController。结果提示由下面的 onTipChanged 镜像回弹窗。
        // 第 4 个参数是对方的显示名 —— AddFriendRsp 只回 request id，不带名字，
        // 控制器要靠它补那条"已发出"的本地记录。
        onFriendRequestRequested: (uid, verifyInfo, remark) => {
            ContactController.sendFriendRequest(uid, verifyInfo, remark, userInfoDialog.userName)
        }

        // 保存本人资料：服务端没有这个接口，先如实说明（接口就绪后换成控制器调用）
        onProfileSaveRequested: (name, sex, desc) => {
            console.log("[chat] 待接入的资料保存请求:", name, sex, desc)
            userInfoDialog.showHint("资料保存接口尚未接入（服务端暂无该接口），本次修改未提交", true)
        }

        // 预留的头像上传接口：接上文件选择 + 上传后，把返回的 URL 写回 avatarUrl
        onAvatarUploadRequested: {
            userInfoDialog.showHint("头像上传接口尚未接入（服务端暂无头像服务）", false)
        }
    }

    /// 仅自动化用：MYCHAT_OPENDLG=sendfriend 时补的第二次「加好友」点击
    Timer {
        id: doubleSendTimer
        interval: 1500
        onTriggered: userInfoDialog.primaryAction()
    }

    /*!
        在线状态查询结果回来 → 刷弹窗上的那一个（列表里的点点是绑定，自己会更新）。

        \note 只认当前这一个 uid：控制器回包不带 uid，所以在飞的那一发是谁，
              得看它说的 uid 跟弹窗里的是不是同一个（对不上就不动）。
    */
    Connections {
        target: PresenceController

        function onStateChanged(uid, online) {
            if (userInfoDialog.visible && uid === userInfoDialog.userId) {
                userInfoDialog.online = online
                userInfoDialog.onlineKnown = true
            }
        }
    }

    /*!
        打开某人资料。

        \a user 可直接传 searchResults / applyList 里的 modelData。
        \a request 传 applyList 里的那一项时，弹窗切成「申请详情」形态
        （只展示那条申请，不再提供「加好友」）；不传则自动去申请列表里找
        有没有跟这个人的往来记录。
    */
    function openUserInfo(user, request) {
        // uid 一律当字符串比：服务端是 uint64，QML 里的 number 只有 53 位精度。
        // ⚠ 优先级 peerUid > uid > fromUid：
        //   申请列表的每项都带 peerUid（对方），而它的 fromUid 在"我发出的"那条里
        //   就是我本人 —— 用 fromUid 会把自己判成"自己"，弹成可编辑的「我的资料」。
        const uid = user.peerUid !== undefined ? String(user.peerUid)
            : (user.uid !== undefined ? String(user.uid)
                : (user.fromUid !== undefined ? String(user.fromUid) : ""))

        userInfoDialog.userName = user.name !== undefined ? user.name : ""
        userInfoDialog.userId = uid
        userInfoDialog.nickName = user.nick !== undefined ? user.nick
            : (user.remark !== undefined ? user.remark : "")
        userInfoDialog.sex = user.sex !== undefined ? user.sex : 2
        userInfoDialog.desc = user.desc !== undefined ? user.desc : ""
        userInfoDialog.avatarUrl = user.icon !== undefined ? user.icon : ""
        // 在线状态：先用缓存（已知就直接显示），未知就先按未知渲染，查询结果回来再刷
        userInfoDialog.onlineKnown = PresenceController.hasState(uid)
        userInfoDialog.online = PresenceController.isOnline(uid)
        PresenceController.refresh(uid)
        // 搜到自己 / 从申请列表点到自己 → 走可编辑形态，免得给自己发好友申请
        userInfoDialog.isSelf = uid.length > 0 && uid === LoginController.uid

        const req = request !== undefined && request !== null
            ? request : page.findRequestFor(uid)
        userInfoDialog.requestInfo = req

        if (req !== null)
            userInfoDialog.showHint(req.received
                ? "对方发来的申请；接受 / 拒绝接口服务端还没提供" : "这条申请是我发出的，等待对方处理", false)
        else
            userInfoDialog.showHint(ContactController.tip, ContactController.tipIsError)

        userInfoDialog.open()
    }

    /// 申请列表里跟这个 uid 有关的往来记录（没有就返回 null）
    function findRequestFor(uid) {
        const list = ContactController.applyList
        for (let i = 0; i < list.length; ++i) {
            if (String(list[i].peerUid) === uid)
                return list[i]
        }
        return null
    }

    /// 批量问一批列表项（搜索结果 / 申请列表）的在线状态。
    /// 控制器与 ChatSocket 都会去重、并串行发（回包不带 uid，不能并发）。
    function refreshPresenceOf(list) {
        for (let i = 0; i < list.length; ++i) {
            const uid = list[i].peerUid !== undefined ? String(list[i].peerUid)
                : (list[i].uid !== undefined ? String(list[i].uid) : "")
            if (uid.length > 0)
                PresenceController.refresh(uid)
        }
    }

    /// 打开自己的资料卡（顶部头像点击）。本人资料服务端不下发，昵称只从会话里取
    function openSelfInfo() {
        userInfoDialog.userName = LoginController.name
        userInfoDialog.userId = LoginController.uid
        userInfoDialog.nickName = ""
        userInfoDialog.sex = 2
        userInfoDialog.desc = ""
        userInfoDialog.avatarUrl = ""
        // 自己当然是"在线且已知" —— 我们正连着聊天服
        userInfoDialog.online = true
        userInfoDialog.onlineKnown = true
        userInfoDialog.isSelf = true
        userInfoDialog.requestInfo = null
        userInfoDialog.showHint("本人的性别 / 签名服务端暂不下发，保存接口也尚未提供", false)
        userInfoDialog.open()
    }

    function doSearch() {
        ContactController.searchUser(keywordEdit.text)
    }

    /*
        自动化验证：MYCHAT_OPENDLG=<other|self|selfedit|sendfriend|applyreceived|applysent>
        时开屏就打开资料弹窗。

        弹窗是 Popup，无头抓图默认抓不到它 —— 有它才能对各个形态分别留证据。
        参数走 main.qml 的 autoDialog（由 main.cpp 从环境变量注入）。
    */
    function openAutoDialog() {
        if (app.autoDialog === "self" || app.autoDialog === "selfedit") {
            page.openSelfInfo()
        } else if (app.autoDialog.startsWith("sendfriend")) {
            // 目标 uid 可选：`sendfriend:<uid>`；不给就用一个格式合法但不存在的 uid
            // —— 用来分别验证「服务端回错误」和「服务端回真实请求编号」两条路。
            const parts = app.autoDialog.split(":")
            const target = parts.length > 1 && parts[1].length > 0 ? parts[1] : "999999999"
            page.openUserInfo({uid: target, name: "自检目标"})
            // 顺带把随申请一起发出去的两条信息填上，日志里就能看到它们真的进了 AddFriendReq
            userInfoDialog.verifyInfo = "自动化自检：验证信息"
            userInfoDialog.remark = "自动化自检：备注"
            // ⚠ refreshApplyList 刚把控制器置 busy，此刻直接发会被静默丢弃
            //   （和 MYCHAT_SEARCH 踩的是同一个竞态）→ 挂起，等 busy 空闲再发
            page.pendingFriendSend = true
            page.tryPendingFriendSend()
            // 回包之后再替用户点一次「加好友」：此刻弹窗应已切成「申请详情」，
            // 这次点击必须是空操作 —— 日志里只应出现一条 AddFriendReq
            doubleSendTimer.restart()
            return
        } else if (app.autoDialog === "applylist") {
            // 只看申请列表本身（不弹资料卡）：验证方向标签与对面 uid 的显示
            page.section = 2
            return
        } else if (app.autoDialog === "applyreceived" || app.autoDialog === "applysent") {
            // 自动化验证：打开申请列表里「收到」/「已发出」的第一条 ——
            // 用来确认弹出来的资料卡是**对方**的，而不是被判成"自己"的编辑态。
            // （服务端列表把「我发出的」也返回，那条的 fromUid 是我本人，是踩过的坑）
            page.section = 2
            page.pendingApplyOpen = app.autoDialog
            page.tryPendingApplyOpen()
            return
        } else {
            page.openUserInfo({
                // 假数据，只为把"他人资料卡"渲染出来；uid 故意避开测试账号 666672，
                // 免得带 MYCHAT_AUTOLOGIN 联调时被判定成"自己"而走了编辑形态
                uid: "666673",
                name: "三花木尼",
                nick: "三花木尼",
                desc: "林中抚琴曲委婉，群山听懂我悲欢",
                sex: 0,
                icon: "",
                online: true
            })
        }

        if (app.autoDialog === "selfedit")
            userInfoDialog.enterEdit()
    }

    Connections {
        target: ContactController

        function onBusyChanged() {
            page.tryAutoSearch()
            page.tryPendingFriendSend()
            page.tryPendingApplyOpen()
        }

        function onApplyListChanged() {
            page.tryPendingApplyOpen()
            page.refreshPresenceOf(ContactController.applyList)
        }

        // 搜索结果出来后顺带问一下各自在不在线（人数很少，一问一个）
        function onSearchResultsChanged() {
            page.refreshPresenceOf(ContactController.searchResults)
        }

        function onValidationFailed(field) {
            if (field === "keyword")
                keywordEdit.invalid = true
        }

        // 弹窗开着时把服务端提示镜像进去（写函数而不是绑属性，避免破坏弹窗里的绑定）
        function onTipChanged() {
            if (userInfoDialog.visible)
                userInfoDialog.showHint(ContactController.tip, ContactController.tipIsError)
        }

        /*
            申请发送成功 → 立刻把弹窗切成「申请详情」。

            ContactController 成功时已经往本地补了一条「已发出」记录，所以这里一定找得到。
            切过去之后主按钮就是禁用的「申请已发送」，同一个弹窗里连点也不会再发 ——
            光靠按钮文案不够，得让状态真的变掉。
        */
        function onFriendRequestSent(ok, error) {
            if (!ok || !userInfoDialog.visible)
                return
            userInfoDialog.requestInfo = page.findRequestFor(userInfoDialog.userId)
        }
    }
}
