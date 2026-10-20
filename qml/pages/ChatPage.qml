import QtQuick
import QtQuick.Controls.Basic
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

    /**
      * 窗口在不在前台（main.qml 绑的 Window.active）。

      ⚠ 后台（切到别的程序 / 最小化）时收到的消息**不算已读**：它们照常攒成红点，
        切回前台才清。否则用户在用别的程序时来的消息会被静默标掉，永远看不到红点。
      */
    property bool windowActive: true

    /*!
        告诉控制器"人现在到底在不在看会话"：窗口在前台、停在「会话」分区、
        **而且视口停在最新那条上**，三条都成立才算。

        只有算"在看"时，收到的消息才会被立刻标成已读（ChatController.attention()）；
        否则攒成侧栏 / 会话行上的红点。
        ⚠ 第三个条件（在底部）是这一版加的：翻着历史时收到的消息必须留着红点，
          否则"往上翻两屏"等于把这段时间的消息全看过了，用户永远不知道有人找。
        ⚠ 切回前台、从别的分区切回会话、或者从历史翻回底部时，把这段时间攒下的
          未读当场清掉 —— 人已经看到最新那条了，再留着红点只是噪音。
      */
    function syncAttention() {
        ChatController.chatVisible = page.windowActive && page.section === 0

        if (ChatController.chatVisible && ChatController.hasActiveConversation
                && messageView.atBottom())
            ChatController.markRead(ChatController.activePeerUid)
    }

    onWindowActiveChanged: page.syncAttention()

    onSectionChanged: {
        page.syncAttention()

        // 进「好友申请」分区 = 这份列表已经摆在眼前了 → 红点清掉
        if (page.section === 2)
            ContactController.markApplyListRead()
    }

    Component.onCompleted: {
        page.syncAttention()

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
    /// 资料卡已经打开、但还没点下去的那次「接受 / 拒绝」（等控制器不忙了再补）
    property string pendingAuthRequestId: ""
    property int pendingAuthState: 0

    /// MYCHAT_SENDMSG 挂起的自动发送：会话还没被打开时先记下来，等选好会话再发
    property bool pendingAutoMessage: false

    /*
     * 选择模式（批量删除本地聊天记录）。
     *
     * selectedIds 用 "id -> true" 的普通对象：QML 里改属性不会触发绑定，
     * 所以每次切换都**整体替换**（Object.assign 出一份新的）再赋值。
     */
    property bool selecting: false
    property var selectedIds: ({})
    property int selectedCount: 0

    /// 自动化用：等历史到位后再执行的选择 / 删除 / 清空动作（见 tryPendingChatAction）
    property string pendingChatAction: ""

    /// 进入选择模式时所在的会话：只有**真的换会话**才退出选择
    property string selectionPeerUid: ""

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

        /*
            ── 左侧导航（QQ 式图标条）──

            原来的 ZSideBar 是 236px 的"品牌 + 文字导航"仪表盘式侧栏，
            占掉了太多横向空间：窗口 960 宽时，会话列表 + 消息区只能挤在剩下
            的 724px 里。按聊天软件的习惯收成 64px 图标条之后，同样的窗口宽度
            能多出 170px 给正文。

            顶部头像 = 自己的资料卡入口（原来是顶部身份条上的那个头像）；
            退出登录、主题切换都收到底部图标里，顶部不再有一条横着的身份栏。
        */
        NavRail {
            id: navRail
            Layout.fillHeight: true
            Layout.preferredWidth: 64

            currentIndex: page.section
            chatBadge: sectionChat.unreadTotal
            applyBadge: ContactController.unreadApplyCount
            selfName: LoginController.name
            selfUid: LoginController.uid

            onActivated: (index) => {
                page.section = index
            }
            onSelfClicked: page.openSelfInfo()
            onThemeClicked: themePopup.open()
            onLogoutClicked: page.logoutRequested()
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

                    /// 当前打开的会话有多少未读（「新消息 ↓」按钮的角标）
                    readonly property int activeUnread: {
                        const uid = String(ChatController.activePeerUid)
                        if (uid.length === 0)
                            return 0
                        const list = ChatController.conversations
                        for (let i = 0; i < list.length; ++i) {
                            if (String(list[i].uid) === uid)
                                return Number(list[i].unread) || 0
                        }
                        return 0
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
                                        onTextChanged: sectionChat.reloadConversations()
                                    }

                                    ZIconButton {
                                        id: searchAdd
                                        glyph: "+"
                                        tooltip: "加好友 / 发起会话"
                                        onClicked: page.section = 1
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

                                /*
                                 * 消息模型（增量同步）。
                                 *
                                 * 不能直接 `model: ChatController.messages`：那是整包
                                 * QVariantList，追加一条也会被当成"模型重置"，ListView
                                 * 把所有 delegate 拆了重建、contentY 归 0 —— 表现就是
                                 * "来了条新消息，正在翻历史的人被甩回最顶部"。
                                 *
                                 * 这里用 ListModel 自己维护：同一会话内只是末尾追加时
                                 * 走 append（视口一动不动）；换会话 / 删除 / 清空才重建。
                                 * 同步逻辑见 page.syncMessageModel()。
                                 */
                                ListModel {
                                    id: messageModel
                                }

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

                                        /*
                                            标题只放文字，头像不重复出现在这里 ——
                                            QQ 的会话头就是"群名 + 人数 / 昵称 + 状态"，头像在左边那列。
                                            要看资料有右边的 ◎ 按钮。
                                        */
                                        Column {
                                            anchors.verticalCenter: parent.verticalCenter
                                            spacing: 3
                                            width: chatHeaderLeft.width

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

                                        ZIconButton {
                                            anchors.verticalCenter: parent.verticalCenter
                                            glyph: "…"
                                            tooltip: "会话操作"
                                            onClicked: chatActionsDialog.open()
                                        }
                                    }
                                }

                                // ── 消息区 ──
                                /*
                                 * 消息列表：Flickable + Column + Repeater。
                                 *
                                 * 为什么不用 ListView：ListView 复用 delegate，视口外的条目高度是按
                                 * 平均行高**估**出来的。本项目的消息气泡高度差得离谱（一行十来个字到
                                 * 整屏 430 字），估算值实测在 4446~21765 之间乱跳：
                                 *   · 拉到底再往上跳，contentHeight 从 5895 炸到 21765、originY 变成
                                 *     -8800、所有 delegate 被回收 —— 屏幕一片空白且不会自愈；
                                 *   · 滚动条按 contentHeight 映射，滑块拖到一半内容就"滚完了"
                                 *     （用户原话："滚动条只能滚动一半"）。
                                 * Column 把每条消息都摆出来，高度是实数：contentHeight 精确、contentY
                                 * 放哪就是哪、滚动条比例天然正确。代价是全部 delegate 常驻内存 ——
                                 * 聊天记录每页 20 条地拉，几百条轻量气泡对这个场景完全够用。
                                 */
                                Flickable {
                                    id: messageView
                                    anchors.top: chatHeader.bottom
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: page.selecting ? selectionBar.top : composer.top
                                    anchors.leftMargin: Theme.spacingLg
                                    anchors.rightMargin: Theme.spacingLg
                                    clip: true
                                    contentWidth: width
                                    contentHeight: msgColumn.height
                                    boundsBehavior: Flickable.StopAtBounds

                                    /// 消息条数（Flickable 没有 count，这里给出和 ListView 一样的读法）
                                    readonly property int count: messageModel.count

                                    /*
                                     * 贴底跟随（聊天软件的习惯）：在底部时新消息跟着滚，
                                     * 人往上翻历史时**不能**把人拽回来。
                                     */
                                    property bool followTail: true

                                    /// 用户正在拖滚动条：这期间的位置变化不算"用户上滚"
                                    property bool _barScrolling: false

                                    /// 正在把自己摁到底：这期间的 contentY 变化不算"用户离开底部"
                                    property bool _snapping: false

                                    /// snapToBottom 补帧次数（新气泡的高度可能晚一帧才算进 Column）
                                    property int _snapTries: 0

                                    /// 上一次记下的 contentY：用来分辨"用户在滚"和"布局自己动了零点几像素"
                                    property real _lastContentY: 0

                                    /// 往上翻页的闸门：一页没落定之前不再拉下一页
                                    property bool _paging: false

                                    /*
                                     * 翻页锚点：视口顶端那条消息是第几条、它离视口顶端多远。
                                     * 更早的一页插进模型头部以后，把同一条消息放回同样的位置 ——
                                     * 屏幕上就是"内容没动，上面接了一页"，而不是整屏往下跳。
                                     */
                                    property int _anchorIndex: -1
                                    property real _anchorOffset: 0

                                    /// 摆视口用的中间量
                                    property int _restoreIndex: -1
                                    property real _restoreOffset: 0
                                    property real _restoreCh: 0
                                    property int _restoreTries: 0

                                    /// 离底部还有多少像素（Column 的高度是实数，这里是精确值）
                                    function distanceToBottom() {
                                        return contentHeight - height - contentY
                                    }

                                    function atBottom() {
                                        return distanceToBottom() <= 8
                                    }

                                    /// 真正能滚到的最远处
                                    function maxContentY() {
                                        return Math.max(0, contentHeight - height)
                                    }

                                    /// 滚停之后把视口收进真实范围（惯性有时会冲过头）
                                    function settleScroll() {
                                        if (contentY > maxContentY())
                                            contentY = maxContentY()
                                    }

                                    /// 把"在不在底部"报给控制器 —— 只有它决定收到的消息算不算已读
                                    function syncReadState() {
                                        ChatController.chatAtBottom = atBottom()
                                    }

                                    /// 用户开始拖滚动条：交出方向盘，别再跟尾
                                    function beginBarScroll() {
                                        _barScrolling = true
                                        followTail = false
                                    }

                                    /// 用户松手：接回方向盘，按真实位置重新判定
                                    function endBarScroll() {
                                        _barScrolling = false
                                        syncReadState()
                                        followTail = atBottom()
                                        settleScroll()
                                    }

                                    /*
                                     * 往上翻到头了 → 找控制器再要一页更早的（20 条）。
                                     *
                                     * 触发条件就是"contentY 贴到顶"：滚轮滚到顶、拖滚动条拖到顶都算。
                                     */
                                    function maybeLoadOlder() {
                                        if (_paging || count === 0 || contentY > 24)
                                            return
                                        // 内容还没一屏长：根本没有"往上翻"这回事
                                        if (atBottom())
                                            return
                                        if (!ChatController.historyHasMore || ChatController.historyLoading)
                                            return

                                        currentAnchor()
                                        // 先上闸门再拉：控制器同步 emit messagesChanged，
                                        // 不挡住就会在同一次 contentY 变化里连拉好几页
                                        _paging = true
                                        pageGuard.restart()
                                        if (ChatController.loadOlderMessages(20) <= 0) {
                                            releasePaging()
                                            _anchorIndex = -1
                                            _anchorOffset = 0
                                        }
                                    }

                                    /// 解锁翻页（加载落定 / 失败 / 兜底超时）
                                    function releasePaging() {
                                        _paging = false
                                        pageGuard.stop()
                                    }

                                    /// 记下"视口顶端那条消息"（翻页后要用它把视口摆回原处）
                                    function currentAnchor() {
                                        for (let i = 0; i < messageModel.count; ++i) {
                                            const item = msgColumn.children[i + 1]
                                            if (item && item.y + item.height > contentY) {
                                                _anchorIndex = i
                                                _anchorOffset = item.y - contentY
                                                return
                                            }
                                        }
                                        _anchorIndex = -1
                                        _anchorOffset = 0
                                    }

                                    /*
                                     * 头部新增 added 条之后，把 _anchorIndex 那条消息放回原处。
                                     *
                                     * 插入时 Column 会整列往下推一页，视口不跟着动就等于"白翻"。
                                     * 这里等下一帧（Column 排完版，y 才是新值）再按真实几何摆回去。
                                     */
                                    function restoreAnchor(added, chBefore) {
                                        if (_anchorIndex < 0) {
                                            releasePaging()
                                            return
                                        }
                                        /*
                                         * 用户正拖着滚动条：这一页是"拖到顶"顺带拉出来的，
                                         * 位置由鼠标说了算。这时候再把视口摆回锚点，等于
                                         * 跟用户的手抢方向盘 —— 实测拖到底松手后，一次
                                         * 翻页的 restore 会把视图拽回锚点（停在 17:06 的
                                         * 老消息上，而不是最新一条）。
                                         */
                                        if (_barScrolling) {
                                            _anchorIndex = -1
                                            _anchorOffset = 0
                                            releasePaging()
                                            return
                                        }
                                        _restoreIndex = Math.min(_anchorIndex + added, count - 1)
                                        _restoreOffset = _anchorOffset
                                        _restoreCh = chBefore
                                        _anchorIndex = -1
                                        _anchorOffset = 0
                                        _restoreTries = 0
                                        restoreTimer.restart()
                                    }

                                    function applyRestore() {
                                        if (_restoreIndex < 0)
                                            return
                                        // Column 还没长高 = 这一页还没排进来，等下一帧
                                        if (contentHeight <= _restoreCh + 0.5 && ++_restoreTries < 8) {
                                            restoreTimer.restart()
                                            return
                                        }
                                        const item = msgColumn.children[_restoreIndex + 1]
                                        _restoreIndex = -1
                                        if (item)
                                            contentY = Math.max(0, item.y - _restoreOffset)
                                        syncReadState()
                                        // 视口已经离开顶部才解锁；还贴着顶就交给 pageGuard 兜底
                                        if (contentY > 24)
                                            releasePaging()
                                    }

                                    /*
                                     * 摁到底。Column 的高度是实数，通常一次赋值就到；
                                     * 但新气泡的高度会晚一帧才落定，所以 snapToBottom 里
                                     * 留了最多 8 帧的补量（不是 ListView 那种无限追帧）。
                                     */
                                    function pinToBottom() {
                                        followTail = true
                                        // 先报"在底部"：人正被带到最新一条，这段时间的消息该算已读
                                        ChatController.chatAtBottom = true
                                        _snapTries = 0
                                        Qt.callLater(snapToBottom)
                                    }

                                    function snapToBottom() {
                                        // 这次位移是我们自己造成的：不能让 onContentYChanged 把它
                                        // 当成"用户离开了底部"，否则 followTail 会被误关掉
                                        _snapping = true
                                        contentY = maxContentY()
                                        _snapping = false
                                        _lastContentY = contentY

                                        // 新气泡的高度常常晚一帧才算进 Column，这一拍量到的
                                        // maxContentY 会短一条 —— 补几帧，和 applyRestore 同理
                                        if (!atBottom() && followTail && !_barScrolling && _snapTries < 8) {
                                            ++_snapTries
                                            snapTimer.restart()
                                        }
                                    }

                                    // 翻页后摆视口：等下一帧再量（Column 这一拍还没排完版）
                                    Timer {
                                        id: restoreTimer
                                        interval: 16
                                        onTriggered: messageView.applyRestore()
                                    }

                                    // 补摁到底：内容高度/位置晚一帧才落定时，下一帧再量一次
                                    Timer {
                                        id: snapTimer
                                        interval: 16
                                        onTriggered: messageView.snapToBottom()
                                    }

                                    // 翻页兜底：400ms 内视口还没摆回去也解锁，
                                    // 宁可多拉一页也不能让「往上翻」彻底失灵
                                    Timer {
                                        id: pageGuard
                                        interval: 400
                                        onTriggered: messageView.releasePaging()
                                    }

                                    onContentHeightChanged: {
                                        // 跟随中内容长高了（新消息 / 换会话铺开）→ 补摁到底
                                        if (followTail && !_barScrolling) {
                                            Qt.callLater(function () {
                                                if (messageView.followTail && !messageView._barScrolling)
                                                    messageView.snapToBottom()
                                            })
                                        }
                                    }

                                    onContentYChanged: {
                                        // 记下这一步走了多远，再更新基准（提前做，
                                        // 免得下面几个 return 让基准过期）
                                        const delta = contentY - _lastContentY
                                        _lastContentY = contentY

                                        // 人在看哪里就报给控制器（决定收到的消息算不算已读）
                                        syncReadState()

                                        // 翻到顶了 → 拉更早的一页（拖滚动条到顶也算）
                                        maybeLoadOlder()

                                        // 用户正在拖滚动条 / 我们正把自己摁到底：
                                        // 这些位置变化不代表"用户想停在这"
                                        if (_barScrolling || _snapping)
                                            return

                                        /*
                                         * 内容一长高，Flickable 会把 contentY 自己抹平零点几像素
                                         * （实测 499.25 → 499）。这不是用户在滚 —— 当成"离底"
                                         * 会把 followTail 关掉，随后那一下补帧就被守卫拦下，
                                         * 新消息永远停在屏幕外面。所以只认真正的一步位移。
                                         */
                                        if (Math.abs(delta) < 1)
                                            return

                                        followTail = atBottom()
                                    }

                                    onHeightChanged: {
                                        // 视口高度变了（进 / 出选择模式、窗口缩放）：本来在底部就继续待在底部
                                        if (followTail)
                                            Qt.callLater(function () { messageView.snapToBottom() })
                                    }

                                    // 滚停（滚轮 / 拖动 / 惯性都算）：把视口收回真实范围
                                    onMovementEnded: {
                                        settleScroll()
                                        // 已经在顶上还继续往上滚时 contentY 不变，上面那个信号不会来
                                        maybeLoadOlder()
                                    }

                                    Column {
                                        id: msgColumn
                                        width: messageView.width
                                        spacing: Theme.spacingMd

                                        // 日期分隔条。真接了历史消息以后按天插，现在只有一段
                                        Item {
                                            width: msgColumn.width
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
                                                    // 翻到头了就直说，省得用户以为"往上翻失灵了"
                                                    // 上面再没有更早的消息时直说；还有就提示"更早" ——
                                                    // 具体是哪一天由下面第一条的时间标签负责，别在这儿猜
                                                    text: (!ChatController.historyHasMore && messageView.count > 0)
                                                          ? "没有更多消息了" : "更早的消息"
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

                                        Repeater {
                                            model: messageModel

                                            /*
                                             * 一行 = 可选的时间标签 + 气泡。
                                             *
                                             * 标签是**默认常显**的（QQ 那种居中灰字），只在
                                             * "第一条 / 跟上一条隔了 5 分钟以上 / 跨天"时出现；
                                             * 气泡旁那个 HH:mm 仍然是悬浮 1 秒才显示，两者不冲突。
                                             */
                                            delegate: Column {
                                                width: msgColumn.width
                                                spacing: Theme.spacingXs

                                                Text {
                                                    anchors.horizontalCenter: parent.horizontalCenter
                                                    visible: text.length > 0
                                                    text: page.stampFor(index, tsMs)
                                                    font: Theme.fontMonoSm
                                                    color: Theme.muted
                                                    opacity: 0.85
                                                }

                                                ChatBubble {
                                                    /*
                                                     * ListModel 的 role 不能叫 text / state ——
                                                     * 那会和 ChatBubble 自己的同名属性打架。所以
                                                     * role 名统一加后缀，在这里转接。
                                                     */
                                                    width: parent.width
                                                    fromSelf: fromMe
                                                    text: body
                                                    time: clock
                                                    state: delivery
                                                    peerName: ChatController.activePeerName
                                                    peerUid: ChatController.activePeerUid
                                                    selfName: LoginController.name
                                                    selfUid: LoginController.uid

                                                    // 右键菜单要按页面坐标系定位
                                                    coordinateSpace: page
                                                    onContextMenuRequested: (x, y) => page.openMessageMenu(msgId, body, x, y)

                                                    // 选择模式：整行可点，点一下切换选中
                                                    selectable: page.selecting
                                                    selected: page.selectedIds[msgId] === true
                                                    onToggleRequested: page.toggleSelection(msgId)
                                                }
                                            }
                                        }
                                    }
                                }

                                // ── 滚动条：贴着消息区右缘（落在右边距里，不压气泡）──
                                ChatScrollBar {
                                    anchors.top: messageView.top
                                    anchors.bottom: messageView.bottom
                                    anchors.left: messageView.right
                                    anchors.leftMargin: Theme.spacingXs
                                    width: 10
                                    target: messageView
                                }

                                /*
                                 * 「新消息 ↓」：人翻着历史时进来的消息。

                                 * 出现条件：当前会话有未读 + 视口不在底部。点它滚到底，滚到底后
                                 * 未读清零（chatAtBottom 变真 → 控制器 markRead），按钮自己消失。
                                 */
                                ZButton {
                                    id: newMessagePill
                                    anchors.horizontalCenter: messageView.horizontalCenter
                                    anchors.bottom: messageView.bottom
                                    anchors.bottomMargin: Theme.spacingMd
                                    visible: messageView.count > 0
                                             && sectionChat.activeUnread > 0
                                             && !messageView.followTail
                                    text: "新消息 ↓"
                                    mono: String(sectionChat.activeUnread)
                                    variant: Theme.Variant.Primary
                                    onClicked: {
                                        // 先滚到底（会把 chatAtBottom 置真），再显式清掉未读，
                                        // 免得滚动还没落定、角标先闪一下
                                        messageView.pinToBottom()
                                        ChatController.markRead(ChatController.activePeerUid)
                                    }
                                }

                                /*
                                 * 轻提示：复制成功 / 已删除这类瞬时反馈，1.6 秒淡出。

                                 * 和「新消息 ↓」用同一套锚点（贴消息区底部居中、比它高一行），
                                 * 这样既压不住输入框，也不会挡住最后一条消息。
                                 * 关掉后 visible 跟着 opacity 走，不占鼠标事件。
                                 */
                                Rectangle {
                                    id: toast
                                    anchors.horizontalCenter: messageView.horizontalCenter
                                    anchors.bottom: messageView.bottom
                                    anchors.bottomMargin: Theme.spacingMd + 44
                                    width: toastLabel.implicitWidth + Theme.spacingLg * 2
                                    height: 34
                                    color: Theme.inkBlock
                                    border.width: Theme.border
                                    border.color: Theme.line
                                    opacity: 0
                                    visible: opacity > 0
                                    z: 20

                                    Behavior on opacity {
                                        NumberAnimation {
                                            duration: 120
                                            easing.type: Theme.easingStd
                                        }
                                    }

                                    Text {
                                        id: toastLabel

                                        anchors.centerIn: parent
                                        color: Theme.textOnSub
                                        font: Theme.fontMonoSm
                                    }
                                }

    // ── 输入区：ZChatInput ──
                                // 面板高度由组件自己算，而且是**固定**的
                                // （标题条 30 + 输入区 minTextHeight 74 + 工具栏 42）：
                                // 写多行不会把消息区顶上去，超出的内容在框内滚动。
                                Column {
                                    id: composer
                                    // 选择模式下让位给下面的操作条
                                    visible: !page.selecting
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: Theme.spacingMd
                                    spacing: Theme.spacingXs

                                    /*
                                     * 提示行：页面临时提示 > 控制器提示
                                     * （控制器提示承载"发送失败：<原因>"这类结果）
                                     * 两者都空时整行隐藏 —— 常态下不再常驻快捷键说明。
                                     *
                                     * 放在输入面板**上面**：排在下面时，ZChatInput 的工具栏
                                     * 是画在自己边界之外的，提示文字会跟"清空 / 发送"那行叠在一起。
                                     */
                                    Text {
                                        width: parent.width
                                        visible: text.length > 0
                                        text: page.composerNotice.length > 0 ? page.composerNotice
                                            : (ChatController.tip.length > 0 ? ChatController.tip : "")
                                        font: Theme.fontMonoSm
                                        color: page.composerNotice.length > 0 || ChatController.tipIsError
                                            ? Theme.danger : Theme.muted
                                        opacity: page.composerNotice.length > 0 || ChatController.tipIsError
                                            ? 1 : 0.75
                                        elide: Text.ElideRight
                                    }

                                    ZChatInput {
                                        id: chatInput
                                        width: parent.width

                                        // 输入框里不放提示语：快捷键说明也是噪音，
                                        // 空着更干净（placeholderText 默认是"输入消息…"，这里盖掉）
                                        placeholderText: ""

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

                                }

                                /*
                                 * ── 选择模式的底部操作条 ──
                                 *
                                 * 和输入区同一个位置、同一套边距：切换时视觉上只是"换了一个底栏"，
                                 * 消息区高度跟着走（messageView.bottom 已按 selecting 切过）。
                                 */
                                Rectangle {
                                    id: selectionBar
                                    visible: page.selecting
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.bottom: parent.bottom
                                    anchors.margins: Theme.spacingMd
                                    height: 52
                                    color: Theme.surface
                                    border.width: Theme.border
                                    border.color: Theme.line

                                    Text {
                                        anchors.left: parent.left
                                        anchors.leftMargin: Theme.spacingMd
                                        anchors.verticalCenter: parent.verticalCenter
                                        text: "已选 " + page.selectedCount + " 条"
                                        font: Theme.fontMonoMd
                                        color: Theme.ink
                                    }

                                    Row {
                                        id: selectionActions
                                        anchors.right: parent.right
                                        anchors.rightMargin: Theme.spacingMd
                                        anchors.verticalCenter: parent.verticalCenter
                                        spacing: Theme.spacingSm

                                        ZButton {
                                            text: "删除"
                                            enabled: page.selectedCount > 0
                                            onClicked: deleteSelectedDialog.open()
                                        }

                                        ZButton {
                                            text: "取消"
                                            variant: Theme.Variant.Secondary
                                            onClicked: page.exitSelection()
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                // ── 1. 联系人：好友列表 + 搜索用户 ──────────────
                /*
                    QQ 式的两栏：中栏是好友列表，右栏是「找人」（搜索 + 结果）。

                    改造前这一页只有搜索框 —— 好友列表原来挂在已被删掉的左侧品牌栏里，
                    导航图标化之后好友就"看不见"了。这里把好友列表放回中栏；
                    点一行仍然打开资料卡（发消息 / 删除都在那儿），不重复做入口。
                */
                Row {
                    id: contactsRow
                    anchors.fill: parent
                    spacing: 0
                    visible: page.section === 1

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
                            contactsRow.reloadFriends()
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
                                onTextChanged: contactsRow.reloadFriends()
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
                                            model: contactsRow.visibleFriends

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
                                                onClicked: page.openUserInfo(modelData)
                                            }
                                        }
                                    }
                                }

                                Text {
                                    anchors.centerIn: parent
                                    visible: contactsRow.visibleFriends.length === 0
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
    /*!
        主题切换浮层（左侧导航底部的「太阳」按钮弹出）。

        以前 ThemeToggle 常驻在 ZSideBar 页脚里；导航收成 64px 图标条之后放不下了，
        改成点图标弹一层。选择仍然由 ThemeToggle 自己写进 QSettings（重启后记得住）。
    */
    Popup {
        id: themePopup
        parent: navRail
        x: parent.width + 6
        // 贴着底部两个辅助图标上方弹出来
        y: parent.height - height - 56
        width: 212
        height: 112
        padding: 0
        modal: false
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

        background: Rectangle {
            color: Theme.surface
            border.width: Theme.border
            border.color: Theme.line
        }

        contentItem: Item {
            implicitWidth: 212
            implicitHeight: 112

            ThemeToggle {
                anchors.fill: parent
                anchors.margins: Theme.spacingMd
            }
        }
    }

    /*!
        会话操作入口（会话头右上角「…」）：选择消息 / 清空聊天记录。

        两项都**只影响本机**：服务端没有"删除 / 撤回"接口 ——
        消息在我们确认收到（1025）时就已经被服务端 XACKDEL 删掉了。
    */
    ZDialog {
        id: chatActionsDialog
        title: "会话操作"
        code: "CHAT-01"
        subtitle: "只影响本机记录"
        showFooter: false
        width: 420

        Column {
            width: parent.width
            spacing: Theme.spacingSm

            ZButton {
                width: parent.width
                text: "选择消息（批量删除）"
                variant: Theme.Variant.Secondary
                onClicked: {
                    chatActionsDialog.close()
                    page.enterSelection()
                }
            }

            ZButton {
                width: parent.width
                text: "清空聊天记录"
                variant: Theme.Variant.Secondary
                onClicked: {
                    chatActionsDialog.close()
                    clearHistoryDialog.open()
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: "聊天记录只存在本机，删除后不可恢复；对方那边不受影响。"
                font: Theme.fontMonoSm
                color: Theme.muted
            }
        }
    }

    /*!
        删除确认（CHAT-02）。两个入口共用这一个弹窗：

        · 选择模式批量删 → singleId = -1，确认后删选中的那批；
        · 右键菜单删一条 → singleId = 那条消息的 id，确认后只删它。
    */
    ZDialog {
        id: deleteSelectedDialog

        /// >0 = 这次只删这一条（右键菜单进来）；-1 = 删选择模式里选中的那批
        property int singleId: -1
        readonly property bool single: singleId > 0

        title: single ? "删除这条消息" : "删除所选消息"
        code: "CHAT-02"
        subtitle: "不可恢复"
        confirmText: single ? "删除" : ("删除 " + page.selectedCount + " 条")
        width: 460
        onConfirmed: {
            if (single)
                page.deleteMessage(singleId)
            else
                page.deleteSelectedMessages()

            // ZDialog 的确认按钮点完会自己 accept()；这里再显式关一次是兜底：
            // 删除会让消息模型整段重建，万一那一拍把按钮的回调吃掉（观察到过），
            // 弹窗就会停在屏幕上，看着像"没生效"。
            deleteSelectedDialog.close()
        }
        // 关掉就清掉单条目标：下次从「选择模式」进来不会误删上一条右键的
        onClosed: deleteSelectedDialog.singleId = -1

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: deleteSelectedDialog.single
                  ? "将从本机删除这条聊天记录。"
                  : "将从本机删除选中的 " + page.selectedCount + " 条聊天记录。"
            font: Theme.fontBody
            color: Theme.ink
        }

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: "服务端没有删除接口，这里删的只是本机这份记录；对方不受影响。"
            font: Theme.fontMonoSm
            color: Theme.muted
        }
    }

    ZDialog {
        id: clearHistoryDialog
        title: "清空聊天记录"
        code: "CHAT-03"
        subtitle: "不可恢复"
        confirmText: "清空"
        width: 460
        onConfirmed: page.clearActiveConversation()

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: "将清空与「"
                + (ChatController.activePeerName.length > 0
                    ? ChatController.activePeerName : ChatController.activePeerUid)
                + "」的全部本地聊天记录。"
            font: Theme.fontBody
            color: Theme.ink
        }

        Text {
            width: parent.width
            wrapMode: Text.Wrap
            text: "对方的记录不受影响；本机删除后无法恢复（服务端没有历史接口）。"
            font: Theme.fontMonoSm
            color: Theme.muted
        }
    }

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

        // 删除好友：两段确认已经在弹窗里做过，这里直接交给控制器发 1027
        onFriendDeleteRequested: (uid) => {
            ContactController.removeFriend(uid)
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

    /*
     * 消息右键菜单：复制 / 删除 / （将来的）添加到表情包。
     *
     * Popup 的 x/y 是相对本页根节点的；右键坐标由气泡 mapToItem 换算过来，
     * 两边同一套坐标系，所以这里直接摆就行。
     */
    ChatMessageMenu {
        id: messageMenu

        /// 当前右键的那一条（复制 / 删除都按它来）
        property string messageId: ""

        onCopyRequested: {
            page.copyToClipboard(messageText)
            page.flashNotice("已复制这条消息")
            close()
        }

        onDeleteRequested: {
            const row_id = Number(messageMenu.messageId)
            messageMenu.close()
            if (row_id <= 0)
                return

            // 先弹确认（复用批量删除那个 CHAT-02），确认后才真的删
            deleteSelectedDialog.singleId = row_id
            deleteSelectedDialog.open()
        }
    }

    /*
     * 剪贴板帮手：QML 没有直接写剪贴板的接口，最省事的做法是让一个
     * 隐藏的 TextEdit 选中全文再 copy() —— 不必为一行复制去改 C++。
     */
    TextEdit {
        id: clipboardHelper
        visible: false
        width: 1
        height: 1
    }

    /// 页面级轻提示的计时器（见 flashNotice）
    Timer {
        id: noticeTimer
        interval: 1600
        onTriggered: toast.opacity = 0
    }

    /// 仅自动化用：MYCHAT_OPENDLG=sendfriend 时补的第二次「加好友」点击
    Timer {
        id: doubleSendTimer
        interval: 1500
        onTriggered: userInfoDialog.primaryAction()
    }

    /// 仅自动化用：MYCHAT_OPENDLG=peerinfo[:uid] 时，等好友列表/会话就位后再开资料卡
    Timer {
        id: peerInfoTimer
        property string targetUid: ""
        interval: 1500
        onTriggered: page.openChatPeerInfo(targetUid)
    }

    /*
     * 仅自动化用：MYCHAT_OPENDLG=chatscroll:<contentY>。
     *
     * 必须延后执行 —— 挂起动作在"消息列表第一次非空"时就会触发，那一刻历史
     * 往往还没灌完，紧接着 activeConversationChanged / syncFriends 又会把
     * followTail 重置成 true 并滚到底，等于白滚。等这一串都落地了再滚。
     */
    Timer {
        id: autoScrollTimer
        property real targetY: 0
        interval: 1200
        onTriggered: {
            messageView.followTail = false
            messageView.contentY = autoScrollTimer.targetY
            console.log("[chat] 自动化：消息列表滚到", messageView.contentY,
                        "contentHeight =", messageView.contentHeight,
                        "followTail =", messageView.followTail)
        }
    }

    /*
     * 仅自动化用：MYCHAT_OPENDLG=chatmenudel / chatmenudelok。
     *
     * 这台机器的鼠标注入时好时坏（锁屏 / 别的进程抢前台时按下会被吞），
     * 没法稳定地点右键菜单，所以这里直接调用菜单信号的发射函数 ——
     * 走的是**和真实点击完全一样**的处理函数，验证
     * 「菜单删除 → 复用批量删除的确认弹窗 → 确认后只删这一条」。
     */
    Timer {
        id: autoMenuTimer
        property string targetAction: ""
        interval: 900
        repeat: false
        onTriggered: {
            messageMenu.deleteRequested()
            if (autoMenuTimer.targetAction === "chatmenudelok")
                autoConfirmTimer.restart()
        }
    }

    /*! 仅自动化用：menuok 流程里替用户点一下确认弹窗的「删除」 */
    Timer {
        id: autoConfirmTimer
        interval: 1400
        repeat: false
        onTriggered: {
            deleteSelectedDialog.confirmed()
            deleteSelectedDialog.accept()
        }
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
        （只展示那条申请，不再提供「加好友」）；不传则**只有对方还有待处理的申请时**
        才自动切过去。已处理的历史申请（比如好友那条「已接受」）不再顶掉资料卡 ——
        否则从会话点头像会弹成「我的申请」。
    */
    /*!
        打开某条消息的右键菜单（气泡的 contextMenuRequested 转进来）。

        @param msgId 消息 id
        @param text  正文（复制用）
        @param x / y 相对本页根节点的落点（气泡那边已经 map 过）

        贴着窗口右/下边缘右键时把菜单往回收一点，别让它跑出画面。
    */
    function openMessageMenu(msgId, text, x, y) {
        messageMenu.messageId = String(msgId)
        messageMenu.messageText = text
        messageMenu.x = Math.max(4, Math.min(x, page.width - messageMenu.width - 4))
        messageMenu.y = Math.max(4, Math.min(y, page.height - messageMenu.height - 4))
        messageMenu.open()
    }

    /*! 写剪贴板（具体理由见 clipboardHelper 那段注释） */
    function copyToClipboard(text) {
        if (text.length === 0)
            return

        clipboardHelper.text = text
        clipboardHelper.selectAll()
        clipboardHelper.copy()
    }

    /*!
        页面级轻提示：浮一个 1.6 秒的 toast（复制成功 / 已删除这类瞬时反馈）。

        为什么不用输入区那行提示（composerNotice）：输入区是底边锚定的，
        那行文字排在工具栏下面，窗口一矮就被挤出画面 —— 复制成功根本看不见。
        浮层跟着窗口走，什么时候都看得见。
    */
    function flashNotice(text) {
        toastLabel.text = text
        toast.opacity = 1
        noticeTimer.restart()
    }

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
        // 已经是好友 → 底部给「删除好友」。必须在打开时算一次：
        // QML 的绑定里"调用函数"不会建立依赖，没法自动跟着好友列表重算
        userInfoDialog.isFriend = !userInfoDialog.isSelf && ContactController.isFriend(uid)
        userInfoDialog.deleteArmed = false

        const req = request !== undefined && request !== null
            ? request : page.findPendingRequestFor(uid)
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
        else if (userInfoDialog.isFriend)
            userInfoDialog.showHint("已是好友，删除后不可撤销", false)
        else
            // 不再直接镜像 ContactController.tip：那可能是上一次搜索 / 刷新留下的
            // 无关提示（"共 1 位好友" 之类），显示在资料卡里会让人以为说错了对象
            userInfoDialog.showHint("", false)

        userInfoDialog.open()
    }

    /*!
        申请列表里「我发给这个人的、还在等处理」的那条（没有就返回 null）。

        重发申请后用它定位控制器刚补的那条记录。**不能按 uid 随便取一条**：
        删好友 → 重新申请时，列表里还留着当年那条「对方发给我、已接受」的旧记录，
        取错了就会显示成"这条申请已经处理过了（已接受）"，和底部「好友申请已发送」矛盾。
    */
    function findOutgoingPendingRequest(uid) {
        const list = ContactController.applyList
        for (let i = 0; i < list.length; ++i) {
            if (String(list[i].peerUid) !== uid)
                continue
            if (list[i].received === true)
                continue
            if (Number(list[i].state) === 0)
                return list[i]
        }
        return null
    }

    /*!
        申请列表里跟这个 uid 有关、且**还在等待处理**的那条（没有就返回 null）。

        为什么资料卡只认"待处理"的申请：成为好友之后，那条「已接受」的历史申请
        仍然留在 applyList 里。如果按"有往来记录就切申请详情"，从会话点头像
        （或点「查看资料」）会直接弹成「我的申请」—— 资料卡根本看不到。
        待处理的申请则相反：这时还没成为好友，切成申请详情才能阻止重复发送。
    */
    function findPendingRequestFor(uid) {
        const list = ContactController.applyList
        for (let i = 0; i < list.length; ++i) {
            if (String(list[i].peerUid) === uid && Number(list[i].state) === 0)
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
        userInfoDialog.isFriend = false
        userInfoDialog.deleteArmed = false
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
        一条消息 → ListModel 的一行。

        role 名故意不叫 text / state：delegate 的根对象是 ChatBubble，本身就有
        这两个属性，同名会冲突。转换在 delegate 里做（见 messageView.delegate）。
    */
    function messageRow(m) {
        return {
            msgId: String(m.id),
            fromMe: m.fromSelf === true,
            body: m.text !== undefined ? m.text : "",
            clock: m.time !== undefined ? m.time : "",
            delivery: m.state !== undefined ? m.state : "",
            // 毫秒时间戳：只用来判断"要不要在气泡上面插一条时间标签"
            tsMs: Number(m.ts !== undefined ? m.ts : 0)
        }
    }

    /// 两条消息隔多久以上才插一条时间标签（QQ 的默认节奏是 5 分钟）
    readonly property int stampGapMs: 5 * 60 * 1000

    /*!
        一条消息的**默认时间标签**（居中、常显，QQ 那种）。

        规则：第一条 / 跟上一条隔了 5 分钟以上 / 跨天 → 显示；挨着的连续聊天不显示
        （每条都戳个时间反而看不清）。取不到时间戳（演示数据）就不显示。
    */
    function stampFor(index, ts) {
        if (!ts || ts <= 0)
            return ""

        if (index === 0)
            return formatStamp(ts)

        const prev = messageModel.get(index - 1)
        const prevTs = prev && prev.tsMs ? prev.tsMs : 0
        if (prevTs > 0 && ts - prevTs < stampGapMs && !isOtherDay(prevTs, ts))
            return ""

        return formatStamp(ts)
    }

    /// 两个时间戳是不是同一天
    function isOtherDay(a, b) {
        const da = new Date(a)
        const db = new Date(b)
        return da.getFullYear() !== db.getFullYear()
            || da.getMonth() !== db.getMonth()
            || da.getDate() !== db.getDate()
    }

    /// 时间戳 → 标签文本：今天只给 HH:mm，更早的带上"昨天 / 月日 / 年月日"
    function formatStamp(ms) {
        const when = new Date(ms)
        const now = new Date()
        const pad = (v) => (v < 10 ? "0" + v : String(v))
        const clock = pad(when.getHours()) + ":" + pad(when.getMinutes())

        const startOfToday = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime()
        const startOfYesterday = startOfToday - 24 * 60 * 60 * 1000

        if (ms >= startOfToday)
            return clock
        if (ms >= startOfYesterday)
            return "昨天 " + clock
        if (when.getFullYear() === now.getFullYear())
            return (when.getMonth() + 1) + "月" + when.getDate() + "日 " + clock
        return when.getFullYear() + "年" + (when.getMonth() + 1) + "月"
            + when.getDate() + "日 " + clock
    }

    /*!
        把 ChatController.messages 同步进 messageModel。

        · **纯追加**（同一会话来了新消息 / 刚发出一条）：只 append 新行，并原地更新
          已有行的状态（sending → sent / failed）。模型不重置，ListView 的 contentY
          原地不动 —— 正在翻历史的人不会被拽走。
        · **头部新增**（往上翻页，更早的一页插进来）：现有模型正好是新列表的
          **后缀**，于是只往头部 insert。重建会把 delegate 全拆了重来，视口当场归零，
          翻页的人会看到屏幕闪一下再跳回去。
        · 其他情况（换会话 / 删除 / 清空 / 首次加载历史）：整体重建。
          重建会丢掉滚动位置，但那几种操作本来就该从头看（换会话后由
          onActiveConversationChanged 统一滚到底）。
        @return 这次往**头部**插了几条（0 = 不是翻页）
    */
    function syncMessageModel(list) {
        /*
         * 先认"头部新增"：现有模型 == 新列表的后缀。
         * 从后往前插，插完顺序才是 list[0..add-1]。
         */
        if (messageModel.count > 0 && messageModel.count < list.length) {
            const add = list.length - messageModel.count
            let suffix_same = true
            for (let i = 0; i < messageModel.count; ++i) {
                if (String(messageModel.get(i).msgId) !== String(list[i + add].id)) {
                    suffix_same = false
                    break
                }
            }

            if (suffix_same) {
                for (let i = add - 1; i >= 0; --i)
                    messageModel.insert(0, messageRow(list[i]))
                // 底下那些行的状态也可能刚变（收到回执），顺手原地更新
                for (let i = messageModel.count - 1; i >= add; --i) {
                    const state = list[i].state !== undefined ? list[i].state : ""
                    if (messageModel.get(i).delivery !== state)
                        messageModel.setProperty(i, "delivery", state)
                }
                return add
            }
        }

        let prefix_same = messageModel.count <= list.length

        if (prefix_same) {
            for (let i = 0; i < messageModel.count; ++i) {
                if (String(messageModel.get(i).msgId) !== String(list[i].id)) {
                    prefix_same = false
                    break
                }
            }
        }

        if (prefix_same) {
            for (let i = 0; i < messageModel.count; ++i) {
                const state = list[i].state !== undefined ? list[i].state : ""
                if (messageModel.get(i).delivery !== state)
                    messageModel.setProperty(i, "delivery", state)
            }
            for (let i = messageModel.count; i < list.length; ++i)
                messageModel.append(messageRow(list[i]))
            return 0
        }

        // 整包重建 = 换了会话 / 换了账号：翻页锚点跟着作废
        messageView._anchorIndex = -1
        messageModel.clear()
        for (let i = 0; i < list.length; ++i)
            messageModel.append(messageRow(list[i]))
        return 0
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

    /*!
        进入 / 退出选择模式（批量删除本地聊天记录）。

        选择只在**当前会话**内有效：切换会话会自动退出（见 onActiveConversationChanged），
        否则选中的 id 会跨会话串味。
    */
    function enterSelection() {
        if (!ChatController.hasActiveConversation)
            return
        page.selectedIds = ({})
        page.selectedCount = 0
        page.selectionPeerUid = ChatController.activePeerUid
        page.selecting = true
    }

    function exitSelection() {
        page.selecting = false
        page.selectedIds = ({})
        page.selectedCount = 0
        page.selectionPeerUid = ""
    }

    /// 点一条气泡切换选中。对象要整体替换，QML 才会重算依赖它的绑定
    function toggleSelection(id) {
        const next = Object.assign({}, page.selectedIds)
        if (next[id])
            delete next[id]
        else
            next[id] = true

        page.selectedIds = next
        page.selectedCount = Object.keys(next).length
    }

    /// 删掉一条（右键菜单确认后调用；只删本机）
    function deleteMessage(id) {
        const row_id = Number(id)
        if (!(row_id > 0))
            return

        if (ChatController.deleteMessages([row_id]) > 0) {
            // 控制器已经把条数写进 tip —— 这里用 toast 顶替，别重复报两遍
            ChatController.clearTip()
            page.flashNotice("已删除这条消息（仅本机）")
        }
    }

    /// 删掉选中的那些（只删本机；调用前已经让用户确认过）
    function deleteSelectedMessages() {
        if (page.selectedCount === 0)
            return

        const ids = Object.keys(page.selectedIds).map((key) => Number(key))
        ChatController.deleteMessages(ids)
        page.exitSelection()
    }

    /// 清空当前会话的聊天记录（只清本机）
    function clearActiveConversation() {
        if (!ChatController.hasActiveConversation)
            return
        ChatController.clearConversation(ChatController.activePeerUid)
        page.exitSelection()
    }

    /*!
        自动化用（MYCHAT_OPENDLG=chatselect:N / chatdelete / chatwipe）。

        这些动作用来做无头截图与链路验证，必须等"会话已打开、历史已到位"才执行 ——
        进页面那一刻好友列表和本地库都还在路上。
    */
    function tryPendingChatAction() {
        if (page.pendingChatAction === "")
            return
        if (!ChatController.hasActiveConversation)
            return

        const list = ChatController.messages
        if (list.length === 0)
            return

        const action = page.pendingChatAction
        page.pendingChatAction = ""

        if (action === "chatwipe") {
            page.clearActiveConversation()
            return
        }

        /*
         * 自动化：把消息列表滚到指定位置（chatscroll:<contentY>）。
         * 用来验证"不在底部时收到新消息，视口不能动"。
         */
        if (action.startsWith("chatscroll")) {
            const parts = action.split(":")
            autoScrollTimer.targetY = parts.length > 1 ? Number(parts[1]) : 0
            autoScrollTimer.restart()
            return
        }

        /*
         * 自动化：右键菜单的「删除」链路（chatmenudel / chatmenudelok）。
         * 先真的把菜单弹出来（验证菜单能定位到这条消息），再隔一拍发
         * deleteRequested —— 等价于用户点了菜单里的「删除」。
         */
        if (action === "chatmenudel" || action === "chatmenudelok") {
            const last = list[list.length - 1]
            page.openMessageMenu(last.id, last.text, 430, 278)
            autoMenuTimer.targetAction = action
            autoMenuTimer.restart()
            return
        }

        let count = 1
        if (action.startsWith("chatselect")) {
            const parts = action.split(":")
            if (parts.length > 1 && !isNaN(Number(parts[1])))
                count = Number(parts[1])
        }

        page.enterSelection()
        for (let i = 0; i < Math.min(count, list.length); ++i)
            page.toggleSelection(list[i].id)

        if (action === "chatdelete")
            page.deleteSelectedMessages()
    }

    /// 会话头部点头像 / 点 ◎：打开这个人的资料卡
    /// \a uid 可选：不传就是当前活动会话的对方（自动化验证时用得到）
    function openChatPeerInfo(uid) {
        const target_uid = uid !== undefined && uid !== null && String(uid).length > 0
            ? String(uid) : String(ChatController.activePeerUid)
        if (target_uid.length === 0)
            return

        const list = ChatController.conversations

        // 会话项里带着完整资料（备注 / 头像 / 签名 / 性别）—— 只传 uid+name 的话
        // 资料卡会少备注，而会话列表显示的正是"备注优先"的名字，对不上。
        for (let i = 0; i < list.length; ++i) {
            if (String(list[i].uid) === target_uid) {
                page.openUserInfo(list[i])
                return
            }
        }

        page.openUserInfo({uid: target_uid, name: ChatController.activePeerName})
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
            // 打开资料卡那一拍，页面可能正忙（onApplyListChanged 里紧跟着的
            // markApplyListRead 会把控制器置忙），这一下点不动 —— 记下来等到
            // busy 落回 false 再补点，否则这个自动化钩子永远静默失败。
            page.pendingAuthRequestId = String(item.id)
            page.pendingAuthState = state
            Qt.callLater(page.commitPendingAuth)
            return
        }

        pendingAuthAction = ""
        console.warn("[chat] 申请列表里没有「收到 + 待处理」的条目，MYCHAT_AUTHAPPLY 跳过")
    }

    /*!
        tryPendingAuth 打开资料卡后，真正替用户点「接受 / 拒绝」。

        弹窗刚拿到 requestInfo，canAuth 要下一拍才算得出来；而控制器这时多半正忙
        （onApplyListChanged 里紧跟着 markApplyListRead 会置忙）。忙就等
        onBusyChanged 再补 —— 只调一次的话这个钩子会静默失效。
    */
    function commitPendingAuth() {
        if (pendingAuthRequestId === "")
            return
        if (ContactController.busy || !userInfoDialog.hasRequest)
            return
        if (String(userInfoDialog.requestInfo.id) !== pendingAuthRequestId)
            return
        const state = pendingAuthState
        pendingAuthRequestId = ""
        userInfoDialog.authRequest(state)
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
            // 还能用「|」串联一个挂起动作：session:<uid>|chatmenudel
            page.section = 0
            const chain = app.autoDialog.split("|")
            const parts = chain[0].split(":")
            if (parts.length > 1 && parts[1].length > 0)
                ChatController.openConversation(parts[1], parts[1])
            if (chain.length > 1 && chain[1].length > 0) {
                page.pendingChatAction = chain[1]
                page.tryPendingChatAction()
            }
            return
        } else if (app.autoDialog.startsWith("peerinfo")) {
            // 自动化验证：打开当前会话好友的资料卡（等好友列表/会话就位再点）
            // 支持 peerinfo:<uid> 指定某个人
            page.section = 0
            const parts = app.autoDialog.split(":")
            peerInfoTimer.targetUid = parts.length > 1 ? parts[1] : ""
            peerInfoTimer.restart()
            return
        } else if (app.autoDialog === "applylist") {
            // 只看申请列表本身（不弹资料卡）：验证方向标签与对面 uid 的显示
            page.section = 2
            return
        } else if (app.autoDialog === "chatactions") {
            // 自动化验证：会话操作弹窗（选择消息 / 清空聊天记录）
            page.section = 0
            chatActionsDialog.open()
            return
        } else if (app.autoDialog === "chatclear") {
            // 自动化验证：清空确认弹窗
            page.section = 0
            clearHistoryDialog.open()
            return
        } else if (app.autoDialog.startsWith("chatselect")
                   || app.autoDialog === "chatdelete"
                   || app.autoDialog === "chatwipe"
                   || app.autoDialog === "chatmenudel"
                   || app.autoDialog === "chatmenudelok"
                   || app.autoDialog.startsWith("chatscroll")) {
            /*
             * 自动化验证：选择 / 删除 / 清空。
             *
             * ⚠ 这些动作要等"会话已打开且历史到了"才能做 —— 进页面的这一刻
             *   好友列表和本地历史都还在路上，直接执行只会对着空列表空转。
             *   所以挂起，由 onMessagesChanged 触发 tryPendingChatAction()。
             */
            page.section = 0
            page.pendingChatAction = app.autoDialog
            page.tryPendingChatAction()
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

        /*!
            发消息被服务端判成「不是好友」（1014 error = 1022）。

            最常见的原因是**对方把我删了** —— 服务端不推送这种变化，只能在发消息时
            撞上。顺手静默刷新好友列表（不覆盖那条"发送失败"提示），列表随之收敛。
        */
        function onPeerIsNotFriend(peerUid) {
            ContactController.refreshFriendListSilently()
        }

        function onMessagesChanged() {
            /*
             * 模型已经在 syncMessageModel() 里做增量同步了（整包 QVariantList 直接当
             * model 会被 QML 当成模型重置，contentY 归 0 —— 那正是"来消息就跳回最顶部"
             * 的根源）。这里只决定**要不要把视口跟到最新**：
             *
             *   · 先量"追加之前人是不是就在底部"：追加本身会让离底距离变大，
             *     追加之后再量一定判成"不在底部"，跟随就永远失效；
             *   · 自己发的：一定看得见；
             *   · 别人发的：人在底部才跟，翻着历史就原地不动，
             *     未读角标由控制器加（见 ChatController::onChatMessages）。
             */
            const list = ChatController.messages
            const last = list.length > 0 ? list[list.length - 1] : null
            const mine = last !== null && last.fromSelf === true
            const wasAtBottom = messageView.atBottom()
            // 翻页后判断"这一页排进来了没有"，靠的是内容总高有没有长
            const chBefore = messageView.contentHeight

            const added = page.syncMessageModel(list)

            // 自动化：等历史到位后执行挂起的选择 / 删除 / 清空（必须放在下面的
            // "不跟随时直接 return" 之前，否则翻着历史就永远轮不到它）
            page.tryPendingChatAction()

            if (added > 0) {
                // 往上翻页：把视口钉回原来那条消息，**绝不**跟到最新
                messageView.restoreAnchor(added, chBefore)
                return
            }

            if (!mine && !wasAtBottom)
                return

            messageView.pinToBottom()
        }

        function onActiveConversationChanged() {
            /*
             * 换会话就退出选择模式（选中的 id 属于上一个会话，串过来会删错）。
             *
             * ⚠ 只在**真的换了人**时退：好友列表同步（syncFriends）也会 emit
             *   activeConversationChanged（名字 / demo 标记可能变了），
             *   早先按"只要这个信号来了就退"，结果刚进选择模式就被那次同步退掉，
             *   截图里操作条根本出不来。
             */
            if (page.selecting && ChatController.activePeerUid !== page.selectionPeerUid)
                page.exitSelection()

            if (ChatController.hasActiveConversation)
                PresenceController.refresh(ChatController.activePeerUid)
            // 换会话一律从最新的看起
            messageView.pinToBottom()

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
            page.commitPendingAuth()
        }

        function onApplyListChanged() {
            page.tryPendingApplyOpen()
            page.tryPendingAuth()
            page.refreshPresenceOf(ContactController.applyList)
            page.syncDialogRequest()

            // 正停在「好友申请」分区：列表一变（比如刷新拉到了新申请）就算已经看过
            if (page.section === 2)
                ContactController.markApplyListRead()
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

            ⚠ 必须按「我发出的 + 待处理」定位：同一个人的历史申请（比如当年他发给我、
              我已接受的那条）还在列表里，按 uid 随便取一条会显示成「已经处理过了」。
        */
        function onFriendRequestSent(ok, error) {
            if (!ok || !userInfoDialog.visible)
                return

            const req = page.findOutgoingPendingRequest(userInfoDialog.userId)
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

        /*!
            删除好友结束：成功就把资料卡关掉（人已经不在好友列表里了）。

            失败时提示已经通过 onTipChanged 镜像进弹窗，这里只保证弹窗还开着。
        */
        function onFriendDeleteFinished(ok, error, uid) {
            if (!ok)
                return

            if (userInfoDialog.visible && userInfoDialog.userId === uid)
                userInfoDialog.close()
        }
    }
}
