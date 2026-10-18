import QtQuick
import QtQuick.Layouts
import ZmdUI 1.0

import MyChat.Core 1.0
import "../components"
import "../dialog"

/*!
    聊天主页 —— 左侧导航（会话 / 联系人 / 好友申请）+ 右侧内容区。

    ⚠ 能力边界（取决于服务端，不是客户端偷懒）：
      · **会话列表**：由**好友列表**驱动（1020/1021），每个好友一个会话入口。
      · **发消息**：已实现（1013 / 1014），气泡状态为「发送中 → 已发送 / 发送失败」。
        ⚠ 回执成功只代表"已写入对方离线消息流"，不代表对方已收到。
      · **收消息**：已实现。1022 只是"你有新消息"的信号（不含内容），
        收到后按节流规则发 1023 拉取、拿 1024 的内容落库、再用 1025 确认；
        登录后也会主动拉一次。⚠ 确认后服务端即删除，**没有历史记录**可翻。
        演示数据只剩 `MYCHAT_DEMO=1` 时才注入，平时界面上没有假数据。
      · **联系人**（搜索用户 / 发好友申请）服务端已实现 → 可用
      · **好友申请列表** 服务端已实现 → 可用

    网络与协议全部交给 ContactController / ChatController，本页不含任何协议细节。

    用户资料卡（UserInfoDialog）的三个入口：
      · 顶部自己的头像 → openSelfInfo() → 可编辑本人资料
      · 搜索结果 / 申请列表点整行 → openUserInfo() → 可发好友申请
      · 会话头部点头像 → openChatPeerInfo()

    发送链路：ZChatInput.onSendRequested → doSendMessage() → ChatController.sendText()
      （控制器负责本地落气泡 + 发 1013 + 用回执改状态，页面只管清输入框）
*/
Item {
    id: page

    /*! 退出登录 / 返回登录页 */
    signal logoutRequested()

    /// 当前分区：0 会话 / 1 联系人 / 2 好友申请
    property int section: 0

    Component.onCompleted: {
        ContactController.refreshApplyList()
        // 好友列表：失败/忙碌都不要紧，refreshFriendList 会排队（见控制器注释）。
        // 会话列表要靠它才有内容，所以两个都发。
        ContactController.refreshFriendList()
        page.initChat()

        // 自动化验证：MYCHAT_SENDMSG 注入的一句话 —— 延到下一拍，
        // 等 initChat() 选好会话、输入框建出来再发
        if (app.autoMessage.length > 0)
            Qt.callLater(page.sendAutoMessage)

        // 自动化验证：MYCHAT_AUTHAPPLY=accept|refuse —— 替用户处理一条收到的好友申请
        if (app.autoAuth.length > 0) {
            page.section = 2
            page.pendingAuthAction = app.autoAuth === "refuse" ? "2" : "1"
            page.tryPendingAuth()
        }

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

    /// MYCHAT_AUTHAPPLY=accept|refuse 时挂起的「接受 / 拒绝」动作（"1" / "2"）
    property string pendingAuthAction: ""

    /// MYCHAT_SENDMSG 挂起的自动发送：会话还没被打开时先记下来，等选好会话再发
    property bool pendingAutoMessage: false

    /// 上一次批量查在线问的是哪几个 uid（拼成字符串）—— 同一批不重复问
    property string presenceKey: ""

    /*!
        输入区的临时提示（附件被退回、超长被拦下……）。

        和 ChatController.tip 分开：那是控制器的业务提示，这里是页面自己发现的
        本地问题；一旦用户继续打字（ZChatInput.textEdited）就清掉。
    */
    property string composerNotice: ""

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
                {
                    label: "会 话", badge: sectionChat.unreadTotal > 0
                        ? String(sectionChat.unreadTotal) : ""
                },
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

            // ── 左下角：主题切换 ────────────────────────────────
            // ZSideBar 的 default property 就是底部页脚，作为子项直接写进来即可，
            // 不用自己算坐标（自己锚的话一改侧栏高度就会压到版本号上）。
            // x 对齐侧栏里其它文字（品牌区、导航行都用 spacingLg 缩进）。
            ThemeToggle {
                x: Theme.spacingLg
                width: parent.width - Theme.spacingLg - Theme.spacingMd
            }
        }

        // ── 右侧内容区 ───────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            /*!
                链路异常提示条（断线 / 收到畸形报文）。

                以前这里是空的：ChatSocket 断连后只是静默丢弃后续请求，
                界面上什么都不会变，用户只会以为"卡住了"；而原因（比如服务端回了
                超长帧）只写进了日志。现在把 LoginController.linkError 挂出来，
                正常时高度为 0，不占地方。
            */
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: visible ? 26 : 0
                visible: LoginController.linkError.length > 0
                color: Theme.surfaceMuted

                // 左侧竖条用危险色，文字也用危险色 —— 比整条红底在深浅两套主题里都稳
                Rectangle {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 3
                    color: Theme.danger
                }

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingMd
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingMd
                    anchors.verticalCenter: parent.verticalCenter
                    text: LoginController.linkError
                    font: Theme.fontMonoSm
                    color: Theme.danger
                    elide: Text.ElideRight
                }
            }

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

                // ── 0. 会话：会话列表 + 消息区 ──────────────────
                Item {
                    id: sectionChat
                    anchors.fill: parent
                    visible: page.section === 0

                    /*!
                        搜索框过滤后的会话（数据源是 ChatController.conversations）。

                        用 property 而不是直接绑 Controller：Repeater 的 model 需要
                        "一变就重建"，而过滤结果是个 JS 数组，得自己算完再赋值。
                    */
                    property var visibleConversations: []

                    /// 全部会话的未读之和（侧边栏角标用；读 conversations 即建立绑定）
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

                    Row {
                        anchors.fill: parent
                        spacing: 0

                        // ══════ 左：会话列表 ══════
                        Rectangle {
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

                                // 头：SESSIONS + 会话数
                                Item {
                                    id: listHeader
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    height: 20

                                    Text {
                                        id: listTitle
                                        anchors.left: parent.left
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "SESSIONS"
                                        font: Theme.fontLabel
                                        color: Theme.muted
                                    }

                                    Text {
                                        anchors.left: listTitle.right
                                        anchors.leftMargin: Theme.spacingSm
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "· " + ChatController.conversationCount
                                        font: Theme.fontMonoSm
                                        color: Theme.muted
                                        opacity: 0.7
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

                                ZTextField {
                                    id: convSearch
                                    anchors.top: listHeader.bottom
                                    anchors.topMargin: Theme.spacingSm
                                    width: parent.width
                                    placeholderText: "搜索会话"
                                    onTextChanged: sectionChat.reloadConversations()
                                }

                                Item {
                                    id: listArea
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: convSearch.bottom
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
                                                model: sectionChat.visibleConversations

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
                                        visible: sectionChat.visibleConversations.length === 0
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
                                    text: sectionChat.hasDemoConversation
                                        ? "含 DEMO · 本地演示数据"
                                        : "FRIENDS · " + ContactController.friendCount + " 位好友"
                                    font: Theme.fontMonoSm
                                    color: Theme.muted
                                    opacity: 0.6
                                }
                            }
                        }

                        // ══════ 右：对话区 ══════
                        Item {
                            id: chatPane
                            width: parent.width - 244
                            height: parent.height

                            // 没选会话：空态（不要留一块空白让人以为渲染坏了）
                            Column {
                                anchors.centerIn: parent
                                spacing: Theme.spacingSm
                                visible: !ChatController.hasActiveConversation

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "「未选择会话」"
                                    font: Theme.fontHeading
                                    color: Theme.ink
                                    opacity: 0.22
                                }

                                Text {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    text: "从左侧选择一个联系人开始对话"
                                    font: Theme.fontBody
                                    color: Theme.muted
                                }
                            }

                            Item {
                                anchors.fill: parent
                                visible: ChatController.hasActiveConversation

                                // ── 会话头：对方是谁 + 在线状态 + 资料入口 ──
                                Rectangle {
                                    id: chatHeader
                                    anchors.top: parent.top
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    height: 64
                                    color: Theme.surface

                                    Rectangle {
                                        anchors.left: parent.left
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        height: 2
                                        color: Theme.line
                                    }

                                    /*
                                     * 左右两簇分开摆：左簇的右边界让给右簇，文字负责省略。
                                     * 不能把头像、名字、按钮塞进同一个 Row ——
                                     * 窗口拉到最小宽度（760）时会话区只剩 280px，
                                     * 它们会直接压在一起。
                                     */
                                    Row {
                                        id: chatHeaderLeft
                                        anchors.left: parent.left
                                        anchors.right: headerActions.left
                                        anchors.leftMargin: Theme.spacingLg
                                        anchors.rightMargin: Theme.spacingMd
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: Theme.spacingMd

                                        ZAvatar {
                                            anchors.verticalCenter: parent.verticalCenter
                                            size: 36
                                            name: ChatController.activePeerName
                                            uid: ChatController.activePeerUid
                                            online: PresenceController.online[ChatController.activePeerUid] === true
                                            clickable: true
                                            onClicked: page.openChatPeerInfo()
                                        }

                                        Column {
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 3
                                            width: chatHeaderLeft.width - 36 - chatHeaderLeft.spacing

                                            Text {
                                                width: parent.width
                                                elide: Text.ElideRight
                                                text: ChatController.activePeerName.length > 0
                                                    ? ChatController.activePeerName
                                                    : ChatController.activePeerUid
                                                font: Theme.fontHeading
                                                color: Theme.ink
                                            }

                                            Text {
                                                width: parent.width
                                                elide: Text.ElideRight
                                                /*
                                                 * 未知就明说"状态未知" —— 只有真查到在线才写"在线"。
                                                 * （弹窗和列表里都是这条规矩：未知 ≠ 离线）
                                                 *
                                                 * ⚠ 第二个参数不是为了用，是为了**建立依赖**：
                                                 *   PresenceController.hasState(uid) 这种函数调用在 QML 眼里
                                                 *   是个黑盒，查到了新状态也不会重算绑定 —— 表现就是
                                                 *   日志里明明回了 offline，界面一直停在"状态未知"。
                                                 *   把 online 这个属性当参数传进去，依赖才挂得上。
                                                 */
                                                text: "UID " + ChatController.activePeerUid + "  ·  "
                                                    + page.peerStateText(ChatController.activePeerUid,
                                                        PresenceController.online)
                                                font: Theme.fontMonoSm
                                                color: Theme.muted
                                            }
                                        }
                                    }

                                    Row {
                                        id: headerActions
                                        anchors.right: parent.right
                                        anchors.rightMargin: Theme.spacingLg
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: Theme.spacingSm

                                        ZTag {
                                            anchors.verticalCenter: parent.verticalCenter
                                            text: "DEMO"
                                            tone: Theme.Tone.Muted
                                            uppercase: false
                                            // 只有点开演示会话时才挂这个标：真实好友的会话挂 DEMO 会误导
                                            visible: ChatController.activePeerIsDemo
                                        }

                                        ZIconButton {
                                            anchors.verticalCenter: parent.verticalCenter
                                            glyph: "◎"
                                            tooltip: "查看资料"
                                            onClicked: page.openChatPeerInfo()
                                        }
                                    }
                                }

                                // ── 消息区 ──
                                ListView {
                                    id: messageView
                                    anchors.top: chatHeader.bottom
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: composer.top
                                    anchors.leftMargin: Theme.spacingLg
                                    anchors.rightMargin: Theme.spacingLg
                                    clip: true
                                    spacing: Theme.spacingMd
                                    model: ChatController.messages
                                    boundsBehavior: Flickable.StopAtBounds

                                    /*
                                     * 贴底跟随（聊天软件的习惯）：有新消息时滚到底，
                                     * 人自己往上翻历史时**不能**把人拽回来。
                                     *
                                     * ⚠ 别退回成"contentHeight 一变就 positionViewAtEnd"。
                                     * ListView 的 contentHeight 在**滚动过程中**也会变：
                                     * 滚出视口的 delegate 被回收、新进视口的先按平均高度估一个，
                                     * 估算值一变信号就响 → 人刚往上滚就被拽回底部。
                                     * 视口越小（窗口最小 760×560 时消息区只剩 ~265px）同时
                                     * 实例化的 delegate 越少、估算抖动越大，症状就是
                                     * "消息区滚不动，死锁在最底部"。
                                     *
                                     * 判定用"离底还有多远"而不是 atYEnd：内容变高时 atYEnd
                                     * 也会变 false，那和我们想抓的"用户往上滚"是两回事。
                                     * 这样滚轮 / 拖拽 / 滚回底部三种情况一套逻辑就够。
                                     */
                                    property bool followTail: true

                                    /*
                                     * "接下来这串位置变化是我们自己造成的"标志。
                                     *
                                     * ListView 在初次布局、delegate 回收 / 重建、模型变化时都会
                                     * 自己改 contentY（首次布局甚至是 header 高度的负值），
                                     * 这些都不是用户上滚。遮一小段时间（pinGuard），
                                     * 期间只认贴底操作，不认"用户意图"。
                                     */
                                    property bool _autoScrolling: true

                                    function distanceToBottom() {
                                        // 顶部 header（"今天"分隔条）把 contentY 的基准整体挪了
                                        // 一个 header 高度，不减掉它算出来的距离恒偏大
                                        const headerH = headerItem ? headerItem.height : 0
                                        return contentHeight - height - contentY - headerH
                                    }

                                    /// 声明"下面这段位置变化不是用户滚的"
                                    function holdUserIntent() {
                                        _autoScrolling = true
                                        pinGuard.restart()
                                    }

                                    function scrollToTail() {
                                        holdUserIntent()
                                        positionViewAtEnd()
                                    }

                                    Timer {
                                        id: pinGuard
                                        interval: 150
                                        onTriggered: messageView._autoScrolling = false
                                    }

                                    // 启动阶段先屏蔽，等第一次布局 + 贴底跑完
                                    Component.onCompleted: holdUserIntent()

                                    onContentHeightChanged: {
                                        /*
                                         * 延一拍再决定要不要贴底 —— 顺序很关键：
                                         * 用户往上一跳时，ListView 会先报 contentHeight 变了
                                         * （新进视口的 delegate 先按平均高度估），紧接着才报
                                         * contentY 变了。当场就滚的话，这一拍 followTail 还是
                                         * true，等于把刚落上去的位置又拽回底部。
                                         * 等这一拍跑完，contentYChanged 已经把 followTail
                                         * 改成 false，这里自然就不滚了。
                                         */
                                        Qt.callLater(function () {
                                            if (messageView.followTail)
                                                messageView.scrollToTail()
                                        })
                                    }

                                    onContentYChanged: {
                                        // 屏蔽窗口之外的位置变化才是"用户滚的"：
                                        // 滚到底就恢复跟随，往上滚就停止跟随。
                                        // （以前这里无脑 positionViewAtEnd，正是这个 bug 的根源）
                                        if (_autoScrolling)
                                            return
                                        followTail = distanceToBottom() <= 8
                                    }

                                    // 日期分隔条。真接了历史消息以后按天插，现在只有一段
                                    header: Item {
                                        width: messageView.width
                                        height: 42

                                        Row {
                                            anchors.centerIn: parent
                                            spacing: Theme.spacingSm

                                            Rectangle {
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: 30
                                                height: 1
                                                color: Theme.lineSoft
                                            }

                                            Text {
                                                anchors.verticalCenter: parent.verticalCenter
                                                text: "今天"
                                                font: Theme.fontMonoSm
                                                color: Theme.muted
                                            }

                                            Rectangle {
                                                anchors.verticalCenter: parent.verticalCenter
                                                width: 30
                                                height: 1
                                                color: Theme.lineSoft
                                            }
                                        }
                                    }

                                    delegate: ChatBubble {
                                        required property var modelData

                                        width: messageView.width
                                        fromSelf: modelData.fromSelf === true
                                        text: modelData.text
                                        time: modelData.time
                                        state: modelData.state
                                        peerName: ChatController.activePeerName
                                        peerUid: ChatController.activePeerUid
                                        selfName: LoginController.name
                                        selfUid: LoginController.uid
                                    }
                                }

                                // ── 输入区：ZChatInput ──
                                // 面板高度由组件自己算，而且是**固定**的
                                // （标题条 30 + 输入区 minTextHeight 74 + 工具栏 42）：
                                // 写多行不会把消息区顶上去，超出的内容在框内滚动。
                                Column {
                                    id: composer
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: Theme.spacingMd
                                    spacing: Theme.spacingXs

                                    ZChatInput {
                                        id: chatInput
                                        width: parent.width

                                        placeholderText: "输入消息，Enter 发送，Shift+Enter 换行…"

                                        // 与服务端对齐：ChatController 里 kMaxTextLength = 500
                                        // （服务端帧 body 只有 2KB）。超限时组件自己拦下不发送。
                                        maxLength: 500

                                        /*
                                         * 定高输入区：高度恒为 minTextHeight（组件默认 74，
                                         * ≈ 3 行），写长了在框内滚、右侧出滚动条，光标始终可见
                                         * —— 这是 ZChatInput 的默认行为，这里显式写出来，
                                         * 免得以后被当成"忘了设"又改成跟着内容长高。
                                         *
                                         * 为什么定高：输入区变高会把消息区顶上去，最小窗口
                                         * （760×560）下消息区本来就只剩 ~265px，写两行长句
                                         * 就把刚发出去的气泡挤出视野了。
                                         */
                                        allowResize: false

                                        /*
                                         * 只发文本：图片 / 文件 / 表情包在协议里虽有 type 值
                                         * （2/3/4），但编码方式（URL、文件 ID）服务端未约定、
                                         * 也没有上传通道 —— 所以两个芯片先关掉，别做出
                                         * "能选能发"的假象。将来有了上传接口再打开。
                                         */
                                        allowImage: false
                                        allowFile: false
                                        // 同理：既然加不了附件，就别常驻写着"可拖入文件"
                                        attachHintText: ""

                                        // 不在发出信号时就清空：ChatController.sendText() 返回 false
                                        // （空串 / 超长）时用户改两个字还能接着发，清掉等于让他重打一遍
                                        clearOnSend: false

                                        onSendRequested: (text, attachments) => page.doSendMessage()

                                        // 拖 / 粘进来的文件仍然会变成附件（组件的 DropArea 不看上面两个开关）
                                        // → 立刻退回并说明，免得"附件看着加上了，发送时悄悄丢掉"
                                        onAttachmentAdded: {
                                            clearAttachments()
                                            page.composerNotice = "服务端尚未实现图片 / 文件消息，附件已退回"
                                        }
                                        onPasteRequested: {
                                            page.composerNotice = "剪贴板里是图片：服务端尚未实现图片消息"
                                        }
                                        onLimitReached: (kind) => {
                                            if (kind === "length")
                                                page.composerNotice = "单条消息最多 "
                                                    + chatInput.maxLength + " 字"
                                        }
                                        onTextEdited: {
                                            // ⚠ 组件是先发 limitReached 再发 textEdited 的，
                                            // 这里无条件清空会把刚弹出来的"超长"提示立刻抹掉 ——
                                            // 所以只在回到上限以内时清。
                                            if (!chatInput.overLimit)
                                                page.composerNotice = ""
                                        }
                                    }

                                    // 提示行：页面临时提示 > 控制器提示 > 常驻快捷键说明
                                    // （控制器提示现在承载"发送失败：<原因>"这类结果）
                                    Text {
                                        width: parent.width
                                        text: page.composerNotice.length > 0 ? page.composerNotice
                                            : (ChatController.tip.length > 0 ? ChatController.tip
                                                : "Enter 发送 · Shift+Enter 换行")
                                        font: Theme.fontMonoSm
                                        color: page.composerNotice.length > 0 || ChatController.tipIsError
                                            ? Theme.danger : Theme.muted
                                        opacity: page.composerNotice.length > 0 || ChatController.tipIsError
                                            ? 1 : 0.75
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                        }
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

        // 接受 / 拒绝收到的好友申请。结果由 onFriendRequestAuthFinished 回填到弹窗底栏
        onFriendAuthRequested: (requestId, state) => {
            ContactController.handleFriendRequest(requestId, state)
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

        if (req !== null) {
            const state = Number(req.state)
            if (state !== 0)
                userInfoDialog.showHint("这条申请已经处理过了（" + req.stateText + "）", false)
            else if (req.received)
                userInfoDialog.showHint("对方发来的好友申请；接受后即成为好友", false)
            else
                userInfoDialog.showHint("这条申请是我发出的，等待对方处理", false)
        }
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

    /// 按申请编号取列表里的那一条（处理结果回来时用来刷新弹窗里的快照）
    function findRequestById(requestId) {
        const list = ContactController.applyList
        for (let i = 0; i < list.length; ++i) {
            if (String(list[i].id) === String(requestId))
                return list[i]
        }
        return null
    }

    /*!
        列表变了就把弹窗里那份申请快照换成最新的。

        弹窗不监听 applyList（展示组件只认 property），所以"状态在别处变了"
        —— 比如推送/刷新把这条改成已接受 —— 得由页面推给它。
    */
    function syncDialogRequest() {
        if (!userInfoDialog.visible || !userInfoDialog.hasRequest)
            return
        const fresh = page.findRequestById(userInfoDialog.requestInfo.id)
        if (fresh !== null)
            userInfoDialog.requestInfo = fresh
    }

    /// 批量问一批列表项（搜索结果 / 申请列表 / 会话）的在线状态。
    /// 控制器与 ChatSocket 都会去重、并串行发（回包不带 uid，不能并发）。
    ///
    /// \a presenceKey 用来记住"上一次问的是这批人"，同一批人不重复问 ——
    /// 会话列表在发一条消息时也会 emit conversationsChanged，没有这道判断就会
    /// 每发一条消息都把全列表的在线状态重问一遍。
    function refreshPresenceOf(list) {
        let key = ""
        for (let i = 0; i < list.length; ++i) {
            const uid = list[i].peerUid !== undefined ? String(list[i].peerUid)
                : (list[i].uid !== undefined ? String(list[i].uid) : "")
            key += uid + ","
        }
        if (key === page.presenceKey)
            return
        page.presenceKey = key

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

    /*!
        会话区初始化：没有选中会话时默认打开第一个，并顺手问一遍各自在不在线。

        为什么进来就自动选一个：真实的 IM 都是"进来就看到上次的对话"，
        停在空态会让人以为功能坏了。
    */
    function initChat() {
        const list = ChatController.conversations
        if (list.length > 0 && !ChatController.hasActiveConversation)
            ChatController.openConversation(String(list[0].uid), list[0].name)

        sectionChat.reloadConversations()
        // 会话条数很少（现在是演示的 4 条）；PresenceController 会串行发，不会把回包搞乱
        page.refreshPresenceOf(list)
    }

    /*!
        发一条消息（ZChatInput 的 sendRequested 与自动化都走这里）。

        控制器返回 false 表示没必要发（空串 / 超长）或没选中会话，这时候
        **不清输入框** —— 用户改两个字还能接着发，清掉等于让他重打一遍。
        成功才用 ZChatInput.clear()（正文 + 残留附件一起清）。
    */
    function doSendMessage() {
        if (!ChatController.hasActiveConversation)
            return

        // 自己发的东西一定要看得见：先把贴底跟随打开，再交给控制器
        // （控制器会同步 emit messagesChanged，顺序反了这口气就白屏）
        messageView.followTail = true

        if (ChatController.sendText(chatInput.text)) {
            chatInput.clear()
            chatInput.focusInput()
        }
    }

    /// 会话头部点头像 / 点 ◎：打开这个人的资料卡
    function openChatPeerInfo() {
        page.openUserInfo({
            uid: ChatController.activePeerUid,
            name: ChatController.activePeerName
        })
    }

    /*!
        会话头那行状态文案。

        \a states 传 PresenceController.online（QVariantMap）—— 只为让绑定依赖它：
        直接调 hasState(uid) 的话，QML 不知道这个函数读了什么，查到了也不会重算。
    */
    function peerStateText(uid, states) {
        const state = states[uid]
        if (state === undefined)
            return "状态未知"
        return state ? "在线" : "离线"
    }

    /*!
        自动化用（MYCHAT_SENDMSG）：把话填进输入框，然后走组件自己的发送链路。

        故意调 chatInput.submit() 而不是 page.doSendMessage()：submit() 正是按
        Enter / 点「发送」时组件内部走的那一步（判空、判超长 → 发 sendRequested），
        这样自检才连"信号接上了没有"一起验掉。

        ⚠ 好友列表（1020/1021）回包比页面开屏晚，所以第一次调用时很可能还没有
          活动会话。这种情况下挂起，等会话被自动打开后再补发（见下面的 Connections）。
    */
    function sendAutoMessage() {
        if (app.autoMessage.length === 0)
            return
        if (!ChatController.hasActiveConversation) {
            page.pendingAutoMessage = true
            return
        }
        page.pendingAutoMessage = false
        chatInput.text = app.autoMessage
        chatInput.submit()
    }

    /*!
        自动化用（MYCHAT_AUTHAPPLY）：挑第一条「收到 + 待处理」的申请，
        打开资料卡后替用户点「接受 / 拒绝」。

        走资料卡而不是直接调控制器，是为了把用户真实经过的那条路一起验掉
        （弹窗底栏按钮 → 页面 → 控制器 → 1010/1011）。
    */
    function tryPendingAuth() {
        if (pendingAuthAction === "" || ContactController.busy)
            return

        const list = ContactController.applyList
        if (list.length === 0)
            return // 等 applyListChanged

        for (let i = 0; i < list.length; ++i) {
            const item = list[i]
            if (item.received !== true || Number(item.state) !== 0)
                continue

            const state = Number(pendingAuthAction)
            pendingAuthAction = ""
            page.openUserInfo(item, item)
            // 弹窗刚拿到 requestInfo，canAuth 要下一拍才算得出来
            Qt.callLater(() => userInfoDialog.authRequest(state))
            return
        }

        pendingAuthAction = ""
        console.warn("[chat] 申请列表里没有「收到 + 待处理」的条目，MYCHAT_AUTHAPPLY 跳过")
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
        } else if (app.autoDialog.startsWith("session")) {
            // 自动化验证：切到「会话」分区并可选地打开某个会话（session:<uid>）
            page.section = 0
            const parts = app.autoDialog.split(":")
            if (parts.length > 1 && parts[1].length > 0)
                ChatController.openConversation(parts[1], parts[1])
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

    /*!
        会话 / 消息：列表与气泡都直接绑 ChatController 的属性，这里只处理
        "数据变了之后界面该跟着做什么"（重建过滤结果、滚到底、查在线）。
    */
    Connections {
        target: ChatController

        function onConversationsChanged() {
            sectionChat.reloadConversations()

            /*
             * 好友列表比页面晚到（页面开屏就发请求，回包是异步的），所以"进来就自动
             * 打开第一个会话"这件事不能只在 initChat() 里做一次 —— 那一刻列表还是空的。
             * 这里补一次：没有活动会话、但有会话了就开第一个。
             */
            const list = ChatController.conversations
            if (!ChatController.hasActiveConversation && list.length > 0)
                ChatController.openConversation(String(list[0].uid),
                    String(list[0].displayName))

            page.refreshPresenceOf(list)
        }

        function onMessagesChanged() {
            /*
             * 新消息一律滚到底？**现在不能这么干了** —— 拉取接通以后，别人发来的
             * 消息会真的出现在列表里；用户翻历史时被一条新消息拽回底部是最烦人的。
             *
             * 规则：
             *   · 自己发的：一定看得见（输入框那边也会先把跟随打开）；
             *   · 别人发的：只有本来就在底部（followTail）才跟，否则原地不动，
             *     未读角标由控制器加（见 ChatController::onChatMessages）。
             *
             * 注意 messages 是整包 QVariantList，追加一条会让 QML 侧当作模型重置，
             * ListView 会把内容位置甩回顶部 —— 所以判断"是不是本来在底部"已经没用了，
             * 得靠 followTail 这个随用户滚动维护的标志。
             */
            const list = ChatController.messages
            const last = list.length > 0 ? list[list.length - 1] : null
            const mine = last !== null && last.fromSelf === true

            if (!mine && !messageView.followTail)
                return

            messageView.followTail = true
            messageView.holdUserIntent()
            Qt.callLater(messageView.scrollToTail)
        }

        function onActiveConversationChanged() {
            if (ChatController.hasActiveConversation)
                PresenceController.refresh(ChatController.activePeerUid)
            // 换会话一律从最新的看起
            messageView.followTail = true
            Qt.callLater(messageView.scrollToTail)

            // MYCHAT_SENDMSG：开屏时列表还是空的，自动发送只能等会话开出来再补
            if (page.pendingAutoMessage)
                Qt.callLater(page.sendAutoMessage)
        }
    }

    Connections {
        target: ContactController

        function onBusyChanged() {
            page.tryAutoSearch()
            page.tryPendingFriendSend()
            page.tryPendingApplyOpen()
            page.tryPendingAuth()
        }

        function onApplyListChanged() {
            page.tryPendingApplyOpen()
            page.tryPendingAuth()
            page.refreshPresenceOf(ContactController.applyList)
            page.syncDialogRequest()
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

            const req = page.findRequestFor(userInfoDialog.userId)
            userInfoDialog.requestInfo = req
            // nick（备注）只有服务端回包/本地回声里有 —— 打开弹窗那一刻还没有，
            // 不补这一句的话，用户刚填的备注在这一屏里看不到。
            if (req !== null && req !== undefined)
                userInfoDialog.nickName = req.nick !== undefined ? req.nick : ""
        }

        /*
            处理（接受 / 拒绝）的回包到了 → 刷新弹窗里那条快照。

            requestInfo 是打开弹窗那一刻的**副本**，控制器改的是 applyList 里那条，
            所以这里必须重新取一份；否则底栏还停在「接受 / 拒绝」上，点第二下就是 1021。
        */
        function onFriendRequestAuthFinished(ok, error, requestId, state) {
            if (!userInfoDialog.visible || !userInfoDialog.hasRequest)
                return
            if (String(userInfoDialog.requestInfo.id) !== String(requestId))
                return

            userInfoDialog.requestInfo = page.findRequestById(requestId)
            userInfoDialog.showHint(ContactController.tip, ContactController.tipIsError)
        }

        /// 对方处理了我发出的申请（1012 推送）→ 弹窗开着这条的话同样刷新
        function onFriendRequestAnswered(requestId, state) {
            if (!userInfoDialog.visible || !userInfoDialog.hasRequest)
                return
            if (String(userInfoDialog.requestInfo.id) !== String(requestId))
                return

            userInfoDialog.requestInfo = page.findRequestById(requestId)
            userInfoDialog.showHint(ContactController.tip, ContactController.tipIsError)
        }
    }
}
