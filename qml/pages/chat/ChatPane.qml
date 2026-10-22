import QtQuick
import ZmdUI 1.0

import MyChat.Core 1.0
import "../../components"
import "../../dialog"

/*!
    会话右栏：会话头 + 消息列表 + 输入区 + 选择模式的操作条。

    这一块原先整个长在 ChatPage 里（3000 行的单文件），拆出来之后 ChatPage 只剩
    「导航 + 分区切换 + 资料卡」。本组件仍然只与控制器打交道，不认识任何协议细节：

      · 发送：ZChatInput.onSendRequested → doSendMessage() → ChatController.sendText()
      · 接收：ChatController.messages 变化 → syncMessageModel()（增量同步 ListModel）
      · 已读：视口贴底 + 窗口在前台 + 停在本分区 → ChatController.chatAtBottom = true

    \note 消息列表本身（气泡 / 翻页 / 滚动条）在 MessageListView.qml 里，它只管
          "把模型摆出来"，选择集合与消息操作弹窗留在这里。
*/

Item {
    id: pane

    // ══════════════════════════════════════════════════════════════
    //  对外状态（ChatPage 读 / 写）
    // ══════════════════════════════════════════════════════════════

    /// 选择模式（批量删除本地记录）：只对当前会话有效
    property bool selecting: false
    /// "id -> true" 的选中集合（QML 里改属性不触发绑定，所以每次整体替换）
    property var selectedIds: ({})
    property int selectedCount: 0
    /// 进入选择模式时所在的会话：只有**真的换会话**才退出选择
    property string selectionPeerUid: ""

    /*!
        输入区的临时提示（附件被退回、超长被拦下……）。

        和 ChatController.tip 分开：那是控制器的业务提示，这里是页面自己发现的
        本地问题；一旦用户继续打字（ZChatInput.textEdited）就清掉。
    */
    property string composerNotice: ""

    /// 自动化用：等历史到位后再执行的选择 / 删除 / 清空动作
    property string pendingChatAction: ""
    /// MYCHAT_SENDMSG 挂起的自动发送：会话还没被打开时先记下来
    property bool pendingAutoMessage: false

    /// 当前会话有多少未读（「新消息 ↓」按钮的角标）
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

    /// 请求打开当前会话对方的资料卡（弹窗由 ChatPage 持有）
    signal peerInfoRequested(string uid)

    /*!
        视口是不是停在最新一条上。

        页面判断"人到底在不在看"要用它（窗口在前台 + 停在会话分区 + 视口贴底，
        三条都成立才算已读）；实现在 MessageListView 里。
    */
    function atBottom() {
        return messageList.atBottom()
    }

    /// 自动化 / 会话头「…」按钮：打开「会话操作」弹窗
    function openActionsDialog() {
        chatActionsDialog.open()
    }

    /// 自动化 / 会话操作弹窗：打开「清空聊天记录」确认框
    function openClearDialog() {
        clearHistoryDialog.open()
    }

    // ══════════════════════════════════════════════════════════════
    //  内容
    // ══════════════════════════════════════════════════════════════


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
                            + pane.peerStateText(ChatController.activePeerUid,
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
                    onClicked: pane.peerInfoRequested(String(ChatController.activePeerUid))
                }

                ZIconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    glyph: "…"
                    tooltip: "会话操作"
                    onClicked: chatActionsDialog.open()
                }
            }
        }

        // ── 消息列表（气泡 / 时间标签 / 滚动条 / 翻页都在组件里）──
        MessageListView {
            id: messageList
            anchors.top: chatHeader.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: pane.selecting ? selectionBar.top : composer.top
            anchors.leftMargin: Theme.spacingLg
            anchors.rightMargin: Theme.spacingLg

            selecting: pane.selecting
            selectedIds: pane.selectedIds
            menuSpace: pane

            onContextMenuRequested: (msgId, text, x, y) => pane.openMessageMenu(msgId, text, x, y)
            onToggleRequested: (msgId) => pane.toggleSelection(msgId)
        }

        /*
         * 「新消息 ↓」：人翻着历史时进来的消息。

         * 出现条件：当前会话有未读 + 视口不在底部。点它滚到底，滚到底后
         * 未读清零（chatAtBottom 变真 → 控制器 markRead），按钮自己消失。
         */
        ZButton {
            id: newMessagePill
            anchors.horizontalCenter: messageList.horizontalCenter
            anchors.bottom: messageList.bottom
            anchors.bottomMargin: Theme.spacingMd
            visible: messageList.count > 0
                     && sectionChat.activeUnread > 0
                     && !messageList.followTail
            text: "新消息 ↓"
            mono: String(sectionChat.activeUnread)
            variant: Theme.Variant.Primary
            onClicked: {
                // 先滚到底（会把 chatAtBottom 置真），再显式清掉未读，
                // 免得滚动还没落定、角标先闪一下
                messageList.pinToBottom()
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
            anchors.horizontalCenter: messageList.horizontalCenter
            anchors.bottom: messageList.bottom
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
            visible: !pane.selecting
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
                text: pane.composerNotice.length > 0 ? pane.composerNotice
                    : (ChatController.tip.length > 0 ? ChatController.tip : "")
                font: Theme.fontMonoSm
                color: pane.composerNotice.length > 0 || ChatController.tipIsError
                    ? Theme.danger : Theme.muted
                opacity: pane.composerNotice.length > 0 || ChatController.tipIsError
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

                onSendRequested: (text, attachments) => pane.doSendMessage()

                // 拖 / 粘进来的文件仍然会变成附件（组件的 DropArea 不看上面两个开关）
                // → 立刻退回并说明，免得"附件看着加上了，发送时悄悄丢掉"
                onAttachmentAdded: {
                    clearAttachments()
                    pane.composerNotice = "服务端尚未实现图片 / 文件消息，附件已退回"
                }
                onPasteRequested: {
                    pane.composerNotice = "剪贴板里是图片：服务端尚未实现图片消息"
                }
                onLimitReached: (kind) => {
                    if (kind === "length")
                        pane.composerNotice = "单条消息最多 "
                            + chatInput.maxLength + " 字"
                }
                onTextEdited: {
                    // ⚠ 组件是先发 limitReached 再发 textEdited 的，
                    // 这里无条件清空会把刚弹出来的"超长"提示立刻抹掉 ——
                    // 所以只在回到上限以内时清。
                    if (!chatInput.overLimit)
                        pane.composerNotice = ""
                }
            }

        }

        /*
         * ── 选择模式的底部操作条 ──
         *
         * 和输入区同一个位置、同一套边距：切换时视觉上只是"换了一个底栏"，
         * 消息区高度跟着走（messageList.bottom 已按 selecting 切过）。
         */
        Rectangle {
            id: selectionBar
            visible: pane.selecting
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
                text: "已选 " + pane.selectedCount + " 条"
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
                    enabled: pane.selectedCount > 0
                    onClicked: deleteSelectedDialog.open()
                }

                ZButton {
                    text: "取消"
                    variant: Theme.Variant.Secondary
                    onClicked: pane.exitSelection()
                }
            }
        }
    }

    // ══════════════════════════════════════════════════════════════
    //  数据变化 → 界面动作
    // ══════════════════════════════════════════════════════════════

    Connections {
        target: ChatController

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
            const wasAtBottom = messageList.atBottom()
            // 翻页后判断"这一页排进来了没有"，靠的是内容总高有没有长
            const chBefore = messageList.contentHeightAll

            const added = messageList.syncMessageModel(list)

            // 自动化：等历史到位后执行挂起的选择 / 删除 / 清空（必须放在下面的
            // "不跟随时直接 return" 之前，否则翻着历史就永远轮不到它）
            pane.tryPendingChatAction()

            if (added > 0) {
                // 往上翻页：把视口钉回原来那条消息，**绝不**跟到最新
                messageList.restoreAnchor(added, chBefore)
                return
            }

            if (!mine && !wasAtBottom)
                return

            messageList.pinToBottom()
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
            if (pane.selecting && ChatController.activePeerUid !== pane.selectionPeerUid)
                pane.exitSelection()

            if (ChatController.hasActiveConversation)
                PresenceController.refresh(ChatController.activePeerUid)
            // 换会话一律从最新的看起
            messageList.pinToBottom()

            // MYCHAT_SENDMSG：开屏时列表还是空的，自动发送只能等会话开出来再补
            if (pane.pendingAutoMessage)
                Qt.callLater(pane.sendAutoMessage)
        }
    }

    // ══════════════════════════════════════════════════════════════
    //  消息操作（发送 / 选择 / 删除 / 右键菜单 / 提示）
    // ══════════════════════════════════════════════════════════════

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
                    pane.enterSelection()
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
        confirmText: single ? "删除" : ("删除 " + pane.selectedCount + " 条")
        width: 460
        onConfirmed: {
            if (single)
                pane.deleteMessage(singleId)
            else
                pane.deleteSelectedMessages()

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
                  : "将从本机删除选中的 " + pane.selectedCount + " 条聊天记录。"
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
        onConfirmed: pane.clearActiveConversation()

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
            pane.copyToClipboard(messageText)
            pane.flashNotice("已复制这条消息")
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
            messageList.followTail = false
            messageList.contentY = autoScrollTimer.targetY
            console.log("[chat] 自动化：消息列表滚到", messageList.contentY,
                        "contentHeight =", messageList.contentHeightAll,
                        "followTail =", messageList.followTail)
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
        messageList.followTail = true

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
        pane.selectedIds = ({})
        pane.selectedCount = 0
        pane.selectionPeerUid = ChatController.activePeerUid
        pane.selecting = true
    }

    function exitSelection() {
        pane.selecting = false
        pane.selectedIds = ({})
        pane.selectedCount = 0
        pane.selectionPeerUid = ""
    }

    /// 点一条气泡切换选中。对象要整体替换，QML 才会重算依赖它的绑定
    function toggleSelection(id) {
        const next = Object.assign({}, pane.selectedIds)
        if (next[id])
            delete next[id]
        else
            next[id] = true

        pane.selectedIds = next
        pane.selectedCount = Object.keys(next).length
    }

    /// 删掉一条（右键菜单确认后调用；只删本机）
    function deleteMessage(id) {
        const row_id = Number(id)
        if (!(row_id > 0))
            return

        if (ChatController.deleteMessages([row_id]) > 0) {
            // 控制器已经把条数写进 tip —— 这里用 toast 顶替，别重复报两遍
            ChatController.clearTip()
            pane.flashNotice("已删除这条消息（仅本机）")
        }
    }

    /// 删掉选中的那些（只删本机；调用前已经让用户确认过）
    function deleteSelectedMessages() {
        if (pane.selectedCount === 0)
            return

        const ids = Object.keys(pane.selectedIds).map((key) => Number(key))
        ChatController.deleteMessages(ids)
        pane.exitSelection()
    }

    /// 清空当前会话的聊天记录（只清本机）
    function clearActiveConversation() {
        if (!ChatController.hasActiveConversation)
            return
        ChatController.clearConversation(ChatController.activePeerUid)
        pane.exitSelection()
    }

    /*!
        自动化用（MYCHAT_OPENDLG=chatselect:N / chatdelete / chatwipe）。

        这些动作用来做无头截图与链路验证，必须等"会话已打开、历史已到位"才执行 ——
        进页面那一刻好友列表和本地库都还在路上。
    */
    function tryPendingChatAction() {
        if (pane.pendingChatAction === "")
            return
        if (!ChatController.hasActiveConversation)
            return

        const list = ChatController.messages
        if (list.length === 0)
            return

        const action = pane.pendingChatAction
        pane.pendingChatAction = ""

        if (action === "chatwipe") {
            pane.clearActiveConversation()
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
            pane.openMessageMenu(last.id, last.text, 430, 278)
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

        pane.enterSelection()
        for (let i = 0; i < Math.min(count, list.length); ++i)
            pane.toggleSelection(list[i].id)

        if (action === "chatdelete")
            pane.deleteSelectedMessages()
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

        故意调 chatInput.submit() 而不是 pane.doSendMessage()：submit() 正是按
        Enter / 点「发送」时组件内部走的那一步（判空、判超长 → 发 sendRequested），
        这样自检才连"信号接上了没有"一起验掉。

        ⚠ 好友列表（1020/1021）回包比页面开屏晚，所以第一次调用时很可能还没有
          活动会话。这种情况下挂起，等会话被自动打开后再补发（见下面的 Connections）。
    */
    function sendAutoMessage() {
        if (app.autoMessage.length === 0)
            return
        if (!ChatController.hasActiveConversation) {
            pane.pendingAutoMessage = true
            return
        }
        pane.pendingAutoMessage = false
        chatInput.text = app.autoMessage
        chatInput.submit()
    }

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
        messageMenu.x = Math.max(4, Math.min(x, pane.width - messageMenu.width - 4))
        messageMenu.y = Math.max(4, Math.min(y, pane.height - messageMenu.height - 4))
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
}
