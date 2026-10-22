import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import ZmdUI 1.0

import MyChat.Core 1.0
import "../components"
import "../dialog"
import "chat"

/*!
    聊天主页 —— 左侧导航（会话 / 联系人 / 好友申请）+ 右侧内容区。

    本文件只留「壳」：导航、分区切换、用户资料卡、自动化入口。三个分区各自成组件
    （见 pages/chat/），它们只跟控制器打交道，通过信号把"要弹资料卡"这类动作回抛：

      · ConversationPane  会话列表（244px 左栏）
      · ChatPane          右栏：会话头 + 消息列表 + 输入区 + 选择模式
        └ MessageListView 气泡 / 时间标签 / 滚动条 / 翻页（Flickable + Column）
      · ContactsPane      联系人：好友列表 + 搜索用户
      · ApplyPane         好友申请列表

    ⚠ 能力边界（取决于服务端，不是客户端偷懒）：
      · **会话列表**：由**好友列表**驱动（1020/1021），每个好友一个会话入口。
      · **发消息**：已实现（1013 / 1014），气泡状态为「发送中 → 已发送 / 发送失败」。
        ⚠ 回执成功只代表"已写入对方离线消息流"，不代表对方已收到。
      · **收消息**：已实现。1022 只是"你有新消息"的信号（不含内容），
        收到后按节流规则发 1023 拉取、拿 1024 的内容落库、再用 1025 确认；
        登录后也会主动拉一次。⚠ 确认后服务端即删除，**没有历史记录**可翻。
        演示数据只剩 `MYCHAT_DEMO=1` 时才注入，平时界面上没有假数据。
      · **联系人 / 好友申请 / 好友备注 / 编辑本人资料** 服务端均已实现 → 可用

    网络与协议全部交给 ContactController / ChatController / LoginController，
    本页不含任何协议细节。

    用户资料卡（UserInfoDialog）的三个入口：
      · 顶部自己的头像 → openSelfInfo() → 可编辑本人资料（1031/1032）
      · 搜索结果 / 申请列表点整行 → openUserInfo() → 可发好友申请
      · 会话头部点头像 → openChatPeerInfo() → 是好友时还能改备注（1029/1030）
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
                && chatPane.atBottom())
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
            Qt.callLater(chatPane.sendAutoMessage)

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
            // 组件自己会处理"控制器正忙"的竞态（挂起到 busyChanged）
            contactsPane.searchWith(app.autoSearch)
        }
    }


    /// MYCHAT_OPENDLG=sendfriend 时挂起的"替用户点一次加好友"
    property bool pendingFriendSend: false

    /// MYCHAT_OPENDLG=applyreceived / applysent 时挂起的"打开申请列表里的某一条"
    property string pendingApplyOpen: ""

    /// MYCHAT_AUTHAPPLY=accept|refuse 时挂起的「接受 / 拒绝」动作（"1" / "2"）
    property string pendingAuthAction: ""
    /// 资料卡已经打开、但还没点下去的那次「接受 / 拒绝」（等控制器不忙了再补）
    property string pendingAuthRequestId: ""
    property int pendingAuthState: 0

    /// 上一次批量查在线问的是哪几个 uid（拼成字符串）—— 同一批不重复问
    property string presenceKey: ""


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
            chatBadge: conversationPane.unreadTotal
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

                    Row {
                        anchors.fill: parent
                        spacing: 0

                        // ══════ 左：会话列表 ══════
                        ConversationPane {
                            id: conversationPane
                            height: parent.height
                            onAddFriendRequested: page.section = 1
                        }

                        // ══════ 右：对话区 ══════
                        ChatPane {
                            id: chatPane
                            width: parent.width - 244
                            height: parent.height
                            onPeerInfoRequested: (uid) => page.openChatPeerInfo(uid)
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
                ContactsPane {
                    id: contactsPane
                    anchors.fill: parent
                    visible: page.section === 1
                    onUserInfoRequested: (user) => page.openUserInfo(user)
                }

                // ── 2. 好友申请列表 ───────────────────────────
                ApplyPane {
                    anchors.fill: parent
                    visible: page.section === 2
                    onUserInfoRequested: (user, request) => page.openUserInfo(user, request)
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

        // 保存本人资料：1031 / 1032。结果回填见下面的 onUserInfoChangeFinished
        onProfileSaveRequested: (name, sex, desc) => {
            LoginController.changeUserInfo(name, desc, sex)
        }

        // 设置好友备注：1029 / 1030。成功后就地更新好友列表（控制器负责）
        onFriendRemarkRequested: (uid, remark) => {
            ContactController.setFriendRemark(uid, remark)
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

    /// 仅自动化用：MYCHAT_OPENDLG=peerinfo[:uid] 时，等好友列表/会话就位后再开资料卡
    Timer {
        id: peerInfoTimer
        property string targetUid: ""
        interval: 1500
        onTriggered: page.openChatPeerInfo(targetUid)
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

    /// 打开自己的资料卡（顶部头像点击）。昵称 / uid 来自会话，签名与性别取本地缓存
    function openSelfInfo() {
        userInfoDialog.userName = LoginController.name
        userInfoDialog.userId = LoginController.uid
        userInfoDialog.nickName = ""
        // 服务端没有"查自己资料"的接口：这两个值只能来自本次登录期间的编辑（1031 回填）
        userInfoDialog.sex = LoginController.selfSex
        userInfoDialog.desc = LoginController.selfDesc
        userInfoDialog.avatarUrl = ""
        // 自己当然是"在线且已知" —— 我们正连着聊天服
        userInfoDialog.online = true
        userInfoDialog.onlineKnown = true
        userInfoDialog.isSelf = true
        userInfoDialog.isFriend = false
        userInfoDialog.deleteArmed = false
        userInfoDialog.requestInfo = null
        userInfoDialog.showHint("昵称 / 签名 / 性别可编辑；头像服务端尚未提供", false)
        userInfoDialog.open()
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

        // 会话条数很少（现在是演示的 4 条）；PresenceController 会串行发，不会把回包搞乱
        page.refreshPresenceOf(list)
    }

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
                chatPane.pendingChatAction = chain[1]
                chatPane.tryPendingChatAction()
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
            chatPane.openActionsDialog()
            return
        } else if (app.autoDialog === "chatclear") {
            // 自动化验证：清空确认弹窗
            page.section = 0
            chatPane.openClearDialog()
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
            chatPane.pendingChatAction = app.autoDialog
            chatPane.tryPendingChatAction()
            return
        } else if (app.autoDialog === "applyreceived" || app.autoDialog === "applysent") {
            // 自动化验证：打开申请列表里「收到」/「已发出」的第一条 ——
            // 用来确认弹出来的资料卡是**对方**的，而不是被判成"自己"的编辑态。
            // （服务端列表把「我发出的」也返回，那条的 fromUid 是我本人，是踩过的坑）
            page.section = 2
            page.pendingApplyOpen = app.autoDialog
            page.tryPendingApplyOpen()
            return
        } else if (app.autoDialog.startsWith("otherfriend")) {
            /*
             * 自动化验证：他人资料卡的「好友态」。
             *
             * 备注编辑行与「删除好友」按钮只有 isFriend 才出现，而这里的假数据没有
             * 真实好友关系 —— 显式置上，用来给这两个控件留离屏证据。
             */
            page.openUserInfo({
                uid: "666674",
                name: "好友甲",
                nick: "老同学",
                desc: "已经是好友的资料卡",
                sex: 1,
                icon: "",
                online: false
            })
            userInfoDialog.isFriend = true
            userInfoDialog.showHint("备注只对自己可见，对方不会收到任何提示", false)
            if (app.autoDialog === "otherfriendedit")
                userInfoDialog.beginEditRemark()
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

    }

    Connections {
        target: ContactController

        function onBusyChanged() {
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

        /*!
            设置备注结束（1030）。

            成功：把新备注定稿到弹窗、退出备注编辑态（好友列表已由控制器就地改好，
            会话列表会跟着 friendListChanged 一起改名）。
            失败：提示由 onTipChanged 镜像，这里只把编辑态留在原地，方便用户改完重试。
        */
        function onFriendRemarkSetFinished(ok, error, uid) {
            if (!userInfoDialog.visible || userInfoDialog.userId !== uid)
                return

            if (ok)
                userInfoDialog.applyRemark(userInfoDialog.remarkDraft)
        }
    }

    /*!
        资料保存结束（1032）。

        只关心"弹窗还开着在改本人资料"这一种情形：成功就把新值写回展示数据
        （弹窗里的昵称 / 签名 / 性别立刻变成刚保存的那份），失败保留编辑态。
    */
    Connections {
        target: LoginController

        function onUserInfoChangeFinished(ok, error) {
            if (!userInfoDialog.visible || !userInfoDialog.isSelf)
                return

            if (ok)
                userInfoDialog.applyProfile(LoginController.name,
                                            LoginController.selfSex,
                                            LoginController.selfDesc)
            else
                userInfoDialog.showHint(LoginController.tip, true)
        }
    }
}
