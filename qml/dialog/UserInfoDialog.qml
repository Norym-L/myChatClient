import QtQuick
import ZmdUI 1.0

import "../components"

/*!
    用户资料弹窗 —— 按「资料卡」的信息层次重排：身份块 → 字段表 → 底部操作。

    三种形态：

    \list
    \li \b 他人资料（默认，isSelf = false）：只读资料 + 底部「加好友」。
        验证信息和备注就在弹窗里填，点「加好友」把
        friendRequestRequested(uid, verifyInfo, remark) 抛给页面，
        由页面转给 ContactController。
        **已经是好友时**多出一行可编辑的「备注」（1029 / 1030：
        friendRemarkRequested(uid, remark)，空串＝清空）和「删除好友」。
    \li \b 我的资料（isSelf = true）：\b 点击头像进入编辑态（昵称 / 签名 / 性别），
        底部按钮变成「保存」→ profileSaveRequested（1031 / 1032）。
        头像上传服务端还没有 —— avatarUploadRequested() 只做提示。
    \li \b 已有申请的详情（requestInfo 非空）：从「好友申请」列表进来时用。
        这时弹窗**只展示那条申请**（方向 / 状态 / 验证信息），
        并且**不再提供「加好友」** —— 申请都已经发出去（或收到了）还让人再点一次，
        除了报个重复申请的错误没有任何意义。
        若这条是「别人发给我、且还在等待处理」，底栏换成「拒绝 / 接受」两个键。
    \endlist

    本文件保持「展示组件」定位：数据靠 property 流入、动作靠 signal 流出，
    busy / hint 两个状态由页面写入，**不直接读写任何控制器**。
    保存成功后由页面调 applyProfile() / applyRemark() 把结果写回来。

    \note 底栏为什么不用 ZDialog 自带的：它固定渲染「取消 + 确认」两个按钮，
          而且确认键点完必定 accept() 关窗 —— 但这里「编辑资料」点完不能关窗，
          「保存」失败时也不能关窗。自己画一份，代价很小、行为完全可控。
          （因此 ZDialog 的 cancelText / confirmText / footerItems / showFooter
            在本文件里都不再参与渲染，留在基类里不碍事。）

    \warning 单向依赖：页面 → 本弹窗 → ZAvatar。若让 ZAvatar 反向 import 本文件，
             QML 会报 "Cyclic dependency detected between ..."，连带 main.qml
             一起加载失败（engine.rootObjects() 为空，进程直接退出）。
*/
ZDialog {
    id: dialog

    /// 比 ZDialog 默认的 540 略宽，容下「头像 + 两列字段」的排布
    width: 560

    // ══════════════════════════════════════════════════════════════
    //  展示数据（由页面写入）
    // ══════════════════════════════════════════════════════════════

    /// 昵称 / 用户名
    property string userName: ""
    /// 用户 id。服务端 uid 是 uint64，这里一律当字符串走
    property string userId: ""
    /// 备注 —— 只对「他人」有意义，是本地给好友起的别名
    property string nickName: ""
    /*!
        性别，原样收服务端的值。

        \note 服务端 `Sex` 枚举是 **0 男 / 1 女 / 2 未知**
              （`GateServer/src/public/data/UserInfo.h`），不是 1 男 2 女。
              这里用 var 而不是 int：QVariantMap 里可能是 int，也可能是字符串。
    */
    property var sex: 2
    /// 个性签名
    property string desc: ""
    /*!
        在线状态。**只有 \l onlineKnown 为真时才有意义** —— 以前服务端不下发在线状态，
        这里一律按 false（离线）显示；现在能查了（CheckIsUserOnlineReq），
        但"还没查到结果"和"查到了、离线"是两件事，不能都显示成 OFFLINE。
    */
    property bool online: false
    /// 在线状态是否已知；false → 界面显示 UNKNOWN，而不是假装离线
    property bool onlineKnown: false
    /*!
        头像地址。服务端还没有头像服务（`UserInfo.icon` 恒为空串），
        这里先把入口留出来：将来上传成功拿到 URL 后，由页面赋值即可显示。
    */
    property url avatarUrl: ""
    /// 是否在看「自己」。true → 可编辑；false → 可加好友
    property bool isSelf: false

    /*!
        对方现在是不是我的好友（由页面按 ContactController.isFriend(uid) 写入）。

        只影响一个按钮：是好友才给「删除好友」。非好友（搜索结果、陌生人）不显示 ——
        服务端会回 1022，点了也只会得到一句错误。
    */
    property bool isFriend: false

    /*!
        已有的好友申请（就是 applyList 里的那一项，null 表示没有）。

        非空时弹窗切到「申请详情」：只展示这条申请，底栏不再给「加好友」。
        字段（由 ContactController 准备好）：id / received / verifyInfo / state / stateText …
    */
    property var requestInfo: null

    /// 是否处于「已有申请」形态
    readonly property bool hasRequest: dialog.requestInfo !== null
        && dialog.requestInfo !== undefined

    /*!
        这条申请现在能不能处理（接受 / 拒绝）。

        三条同时成立才行：是**对方发给我的**、状态还是**待处理 0**、当前没有在提交中。
        自己发出的申请、已经处理过的申请，都不该再给按钮 —— 服务端会回 1021。
    */
    readonly property bool canAuth: dialog.hasRequest
        && dialog.requestInfo.received === true
        && Number(dialog.requestInfo.state) === 0

    // ══════════════════════════════════════════════════════════════
    //  状态（由页面写入）
    // ══════════════════════════════════════════════════════════════

    /// 网络忙碌态：加好友过程中禁用按钮，防止重复提交
    property bool busy: false
    /*!
        弹窗内的一行提示。**由页面调用 showHint() 写，不走属性绑定** ——
        绑定一旦被 JS 赋值就会永久断开，页面后面就再也同步不上服务端提示了。
    */
    property string hint: ""
    property bool hintIsError: false

    // ══════════════════════════════════════════════════════════════
    //  内部状态
    // ══════════════════════════════════════════════════════════════

    /// 是否处于编辑态（只有 isSelf 时有意义）
    property bool editing: false
    /*!
        「删除好友」是否已经按过一次、正在等确认。

        删除是不可撤销的（服务端双向删除），所以做成两段式：第一次点变「确认删除」，
        4 秒内不点就自动复位 —— 别让一个误触把好友删掉。
    */
    property bool deleteArmed: false
    /// 编辑态里选中的性别（0 男 / 1 女 / 2 不公开）
    property int editSex: 2
    /// 加好友时提交的验证信息
    property string verifyInfo: ""
    /*!
        加好友时一起提交的备注（给对方起的别名）。

        \note 服务端的 `AddFriendReq` 本来就有 remark 字段
              （`CALL send_friend_request(?, ?, ?, ?)`），之前客户端一直传空串，
              等于白扔了这个能力。
    */
    property string remark: ""

    /*!
        正在改好友备注（**已是好友**时才有的形态）。

        与上面的 `remark` 不是一回事：那个是「加好友申请」随申请发出去的备注，
        这个是「已经是好友了，改我给对方起的名字」（1029 / 1030）。
    */
    property bool remarkEditing: false
    /// 备注输入框的草稿（点「保存」时随信号抛给页面）
    property string remarkDraft: ""

    // ══════════════════════════════════════════════════════════════
    //  动作出口
    // ══════════════════════════════════════════════════════════════

    /// 请求发送好友申请（页面 → ContactController.sendFriendRequest）
    signal friendRequestRequested(string uid, string verifyInfo, string remark)

    /*!
        请求接受 / 拒绝一条收到的好友申请（页面 → ContactController.handleFriendRequest）。

        \a state：1 = 接受，2 = 拒绝（服务端只认这两个值）。
    */
    signal friendAuthRequested(string requestId, int state)

    /// 请求保存本人资料。服务端 1031/1032 已实现，页面转给 LoginController
    signal profileSaveRequested(string name, int sex, string desc)

    /// 请求设置好友备注（页面 → ContactController.setFriendRemark）
    signal friendRemarkRequested(string uid, string remark)

    /*!
        预留：请求上传 / 更换头像。

        页面接到后接「文件选择框 → 上传 → 把返回的 URL 写回 avatarUrl」即可，
        本文件不用再改。当前服务端没有头像服务，页面只会提示接口未接入。
    */
    signal avatarUploadRequested()

    /*!
        请求删除这位好友（页面 → ContactController.removeFriend）。

        只在 isFriend && !isSelf && !hasRequest 时可能发出；弹窗自己不做网络，
        也不做"删完关窗"这类判断 —— 那些都在页面那一侧。
    */
    signal friendDeleteRequested(string uid)

    // ══════════════════════════════════════════════════════════════
    //  外壳
    // ══════════════════════════════════════════════════════════════

    title: dialog.hasRequest
        ? (dialog.requestInfo.received ? "好友申请" : "我的申请")
        : (dialog.isSelf ? "我的资料" : "用户资料")
    code: dialog.hasRequest ? "REQ-" + dialog.requestInfo.id
        : (dialog.isSelf ? "SELF-" : "USER-") + dialog.userId

    /// 字段名的固定列宽：让所有字段的值左边缘对齐（工业面板的铭牌感）
    readonly property int labelWidth: 64

    /// 「删除好友」第一次点击的提示；也是判断 hint 是不是自己那句的依据
    readonly property string deleteHintText: "再点一次「确认删除」：双方好友关系都会被移除（本地聊天记录保留）"

    onClosed: {
        dialog.editing = false
        dialog.deleteArmed = false
        dialog.hint = ""
        dialog.hintIsError = false
        dialog.verifyInfo = ""
        dialog.remark = ""
        dialog.remarkEditing = false
        dialog.remarkDraft = ""
        dialog.requestInfo = null
        // TextField 的 text 是单向写入的（见下面 onTextChanged），得手动清
        verifyEdit.text = ""
        remarkEdit.text = ""
    }

    // ══════════════════════════════════════════════════════════════
    //  内容
    //
    //  ZDialog 会把这里的每个子项都塞进它自己的 Column，行距是固定的
    //  spacingMd。想自己控行距就只放一个 Column，在里面再排。
    // ══════════════════════════════════════════════════════════════

    Column {
        id: body
        width: parent.width
        spacing: Theme.spacingLg

        // ── ① 身份块：头像 + 昵称 + UID ──────────────────────────
        Row {
            id: identity
            width: parent.width
            spacing: Theme.spacingLg

            // 头像。自己的头像可点：未编辑 → 进编辑态；编辑中 → 换头像
            Item {
                id: avatarBox
                width: 92
                height: 92

                ZAvatar {
                    id: avatar
                    anchors.fill: parent
                    size: avatarBox.width
                    name: dialog.userName
                    uid: dialog.userId
                    // 未知时不点亮，免得看着像"在线"
                    online: dialog.onlineKnown && dialog.online
                    source: dialog.avatarUrl
                    clickable: dialog.isSelf
                    onClicked: dialog.toggleEdit()
                }

                // 编辑态盖一层：明确告诉用户「这儿的头像是可以换的」。
                // 用斜切矩形而不是圆形遮罩，跟 ZmdUI 的切角语言保持一致。
                ZCutRect {
                    anchors.fill: parent
                    visible: dialog.editing
                    color: Qt.rgba(0, 0, 0, 0.66)
                    strokeColor: Theme.accent
                    strokeWidth: Theme.border
                    cutTopRight: Theme.cutMd
                    cutBottomLeft: Theme.cutSm

                    Column {
                        anchors.centerIn: parent
                        spacing: 2

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "更换头像"
                            font: Theme.fontStrong
                            color: Theme.accent
                        }
                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: "UPLOAD"
                            font: Theme.fontMonoSm
                            color: Theme.textOnSub
                            opacity: 0.75
                        }
                    }
                }
            }

            Column {
                id: identityText
                width: identity.width - avatarBox.width - identity.spacing
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingSm

                Text {
                    width: parent.width
                    text: dialog.userName.length > 0 ? dialog.userName : "（未命名）"
                    font: Theme.fontHeading
                    color: Theme.ink
                    elide: Text.ElideRight
                }

                Row {
                    spacing: Theme.spacingSm

                    ZTag {
                        text: "UID " + (dialog.userId.length > 0 ? dialog.userId : "—")
                        tone: Theme.Tone.Ink
                    }

                    ZTag {
                        text: !dialog.onlineKnown ? "UNKNOWN"
                            : (dialog.online ? "ONLINE" : "OFFLINE")
                        tone: dialog.onlineKnown && dialog.online
                            ? Theme.Tone.Accent : Theme.Tone.Muted
                        dotColor: dialog.onlineKnown && dialog.online
                            ? Theme.cyan : "transparent"
                    }
                }

                // 自己：提示头像可点。不常驻装饰，只在"还能编辑"时露面。
                // ⚠ 用 Theme.muted 而不是 Theme.accent —— 亮色主题下 accent 是 #E8FA00，
                //   把亮黄当"白底上的文字色"几乎看不见（ZmdUI 里 accent 是给底色用的）。
                Text {
                    width: parent.width
                    visible: dialog.isSelf && !dialog.editing
                    text: "› 点击头像可编辑资料"
                    font: Theme.fontMonoSm
                    color: Theme.muted
                }
            }
        }

        // ── ② 字段表（只读态）────────────────────────────────────
        Column {
            id: infoList
            width: parent.width
            visible: !dialog.editing

            Repeater {
                model: dialog.infoRows()

                delegate: Item {
                    required property var modelData

                    width: infoList.width
                    // 行高 = 内容高 + 上下留白；分隔线画在行的顶边，首行也算，正好当分节线
                    height: infoRow.height + Theme.spacingMd * 2

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: 1
                        color: Theme.lineSoft
                    }

                    Row {
                        id: infoRow
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: Theme.spacingMd
                        spacing: Theme.spacingMd

                        Text {
                            width: dialog.labelWidth
                            text: modelData.label
                            font: Theme.fontLabel
                            color: Theme.muted
                        }

                        Text {
                            width: infoRow.width - dialog.labelWidth - infoRow.spacing
                            text: modelData.value.length > 0 ? modelData.value : "—"
                            font: Theme.fontBody
                            color: Theme.inkSoft
                            wrapMode: Text.WordWrap
                            lineHeight: 1.3
                        }
                    }
                }
            }
        }

        // ── ②b 好友备注（可编辑，仅「已经是好友」时）──────────────
        /*
         * 为什么单独一块而不是塞进 infoRows()：字段表是 Repeater + 纯文本，
         * 加按钮会把「展示数据」和「交互控件」混在同一个 model 里。
         * 这里保持和字段表同样的行高 / 标签列宽，视觉上仍是同一张表。
         */
        Column {
            id: remarkBlock
            width: parent.width
            visible: !dialog.isSelf && !dialog.hasRequest && dialog.isFriend && !dialog.editing
            spacing: Theme.spacingSm

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                height: 1
                color: Theme.lineSoft
            }

            Row {
                width: parent.width
                spacing: Theme.spacingMd

                Text {
                    width: dialog.labelWidth
                    anchors.verticalCenter: parent.verticalCenter
                    text: "备注"
                    font: Theme.fontLabel
                    color: Theme.muted
                }

                Text {
                    width: parent.width - dialog.labelWidth - remarkEditBtn.width
                             - parent.spacing * 2
                    anchors.verticalCenter: parent.verticalCenter
                    text: dialog.nickName.length > 0 ? dialog.nickName : "未设置"
                    font: Theme.fontBody
                    color: dialog.nickName.length > 0 ? Theme.inkSoft : Theme.muted
                    elide: Text.ElideRight
                }

                ZButton {
                    id: remarkEditBtn
                    anchors.verticalCenter: parent.verticalCenter
                    text: dialog.remarkEditing ? "取消" : "修改"
                    variant: Theme.Variant.Secondary
                    enabled: !dialog.busy
                    onClicked: dialog.remarkEditing ? dialog.cancelRemarkEdit()
                                                    : dialog.beginEditRemark()
                }
            }

            Row {
                width: parent.width
                spacing: Theme.spacingSm
                visible: dialog.remarkEditing

                ZTextField {
                    id: remarkField
                    width: parent.width - remarkSaveBtn.width - parent.spacing
                    label: "新备注"
                    unit: "REMARK"
                    placeholderText: "留空表示清除备注（最多 64 字）"
                    enabled: !dialog.busy
                    onAccepted: dialog.submitRemark()
                }

                ZButton {
                    id: remarkSaveBtn
                    anchors.bottom: remarkField.bottom
                    text: dialog.busy ? "保存中…" : "保存"
                    variant: Theme.Variant.Primary
                    enabled: !dialog.busy
                    onClicked: dialog.submitRemark()
                }
            }
        }

        // ── ③ 编辑态表单（仅自己）────────────────────────────────
        Column {
            id: editForm
            width: parent.width
            spacing: Theme.spacingMd
            visible: dialog.editing

            ZTextField {
                id: nameEdit
                width: parent.width
                label: "昵称"
                unit: "NAME"
                placeholderText: "输入新的昵称"
                enabled: !dialog.busy
            }

            ZTextField {
                id: descEdit
                width: parent.width
                label: "个性签名"
                unit: "SIGN"
                placeholderText: "一句话介绍自己"
                enabled: !dialog.busy
            }

            Row {
                width: parent.width
                height: Theme.dp(Theme.controlHeightSm)
                spacing: Theme.spacingMd

                Text {
                    width: dialog.labelWidth
                    anchors.verticalCenter: parent.verticalCenter
                    text: "性别"
                    font: Theme.fontLabel
                    color: Theme.muted
                }

                // autoExclusive 默认 true，同一个 parent 下自动互斥。
                // ⚠ 不写 `checked: x === 0` 这种绑定 —— 用户点一下就把它打断了，
                //   下次进编辑态不会重新求值。改成进编辑态时显式赋值。
                ZRadioButton {
                    id: sexMale
                    anchors.verticalCenter: parent.verticalCenter
                    text: "男"
                    enabled: !dialog.busy
                    onToggled: if (checked) dialog.editSex = 0
                }
                ZRadioButton {
                    id: sexFemale
                    anchors.verticalCenter: parent.verticalCenter
                    text: "女"
                    enabled: !dialog.busy
                    onToggled: if (checked) dialog.editSex = 1
                }
                ZRadioButton {
                    id: sexUnknown
                    anchors.verticalCenter: parent.verticalCenter
                    text: "不公开"
                    enabled: !dialog.busy
                    onToggled: if (checked) dialog.editSex = 2
                }
            }

            Row {
                width: parent.width
                height: Theme.dp(Theme.controlHeight)
                spacing: Theme.spacingMd

                Text {
                    width: dialog.labelWidth
                    anchors.verticalCenter: parent.verticalCenter
                    text: "头像"
                    font: Theme.fontLabel
                    color: Theme.muted
                }

                ZButton {
                    anchors.verticalCenter: parent.verticalCenter
                    text: "上传新头像"
                    variant: Theme.Variant.Secondary
                    enabled: !dialog.busy
                    onClicked: dialog.requestAvatarUpload()
                }

                ZTag {
                    anchors.verticalCenter: parent.verticalCenter
                    text: dialog.avatarUrl.toString().length > 0 ? "已设置" : "待接入"
                    tone: Theme.Tone.Muted
                }
            }
        }

        // ── ④ 加好友随申请发出去的两条信息（仅"可以发申请"时）─────
        Column {
            width: parent.width
            // 已经是好友 / 已有申请时都没有"发申请"这回事，输入框留着只会误导
            visible: !dialog.isSelf && !dialog.hasRequest && !dialog.isFriend
            spacing: Theme.spacingMd

            ZTextField {
                id: verifyEdit
                width: parent.width
                label: "验证信息"
                unit: "VERIFY"
                placeholderText: "打个招呼，说明你是谁（可留空）"
                enabled: !dialog.busy
                // 单向同步：只把输入写进属性，不回读，避免和 text 形成循环绑定
                onTextChanged: dialog.verifyInfo = text
            }

            ZTextField {
                id: remarkEdit
                width: parent.width
                label: "备注"
                unit: "REMARK"
                placeholderText: "给对方起个备注名，加好友后好认（可留空）"
                enabled: !dialog.busy
                onTextChanged: dialog.remark = text
            }
        }
    }

    // ══════════════════════════════════════════════════════════════
    //  底栏
    // ══════════════════════════════════════════════════════════════

    footer: Item {
        implicitHeight: 64

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacingLg
            anchors.rightMargin: Theme.spacingXl
            height: 2
            color: Theme.line
        }

        // 左侧提示：服务端结果 / 接口未接入说明，都走这里
        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingLg
            anchors.right: footerButtons.left
            anchors.rightMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            visible: dialog.hint.length > 0
            text: dialog.hint
            font: Theme.fontBody
            color: dialog.hintIsError ? Theme.danger : Theme.muted
            elide: Text.ElideRight
        }

        Row {
            id: footerButtons
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSm

            /*
             * 删除好友：只对「已经是好友的别人」显示。
             *
             * 两段式确认（见 requestDeleteFriend）：第一次点只是把按钮换成「确认删除」，
             * 4 秒内不点自动复位。删除不可撤销 —— 服务端会双向删掉关系和备注。
             */
            ZButton {
                text: dialog.deleteArmed ? "确认删除" : "删除好友"
                variant: Theme.Variant.Danger
                visible: dialog.isFriend && !dialog.isSelf && !dialog.hasRequest
                         && !dialog.editing
                enabled: !dialog.busy
                onClicked: dialog.requestDeleteFriend()
            }

            ZButton {
                text: dialog.editing ? "取消" : "关闭"
                variant: Theme.Variant.Secondary
                onClicked: {
                    if (dialog.editing)
                        dialog.exitEdit()
                    else
                        dialog.close()
                }
            }

            /*
             * 待处理的申请：底栏换成「拒绝 / 接受」。
             *
             * 点完**不关窗**：结果（已接受 / 对方拒绝过 / 重复处理 1021）得留在这块底栏上，
             * 关掉用户就只看到列表里一行变了，不知道发生了什么。
             */
            ZButton {
                text: "拒绝"
                variant: Theme.Variant.Secondary
                visible: dialog.canAuth
                enabled: !dialog.busy
                onClicked: dialog.authRequest(2)
            }

            ZButton {
                text: dialog.busy ? "提交中…" : "接受"
                variant: Theme.Variant.Primary
                visible: dialog.canAuth
                enabled: !dialog.busy
                onClicked: dialog.authRequest(1)
            }

            ZButton {
                visible: !dialog.canAuth
                text: dialog.primaryText()
                variant: Theme.Variant.Primary
                // 已有申请 / 已经是好友时禁用：再发一次只会被服务端顶回来（1016 / 1018）
                enabled: !dialog.busy && !dialog.hasRequest && !dialog.isFriend
                onClicked: dialog.primaryAction()
            }
        }

        /// 「删除好友」的两段确认：4 秒不点就自动复位，免得误触
        Timer {
            id: deleteArmTimer
            interval: 4000
            onTriggered: dialog.resetDeleteArmed()
        }
    }

    // ══════════════════════════════════════════════════════════════
    //  逻辑
    // ══════════════════════════════════════════════════════════════

    /// 服务端 Sex → 0 / 1 / 2。入参可能是 int，也可能是 "男" 这类中文
    function sexValue() {
        const value = String(dialog.sex).trim()
        if (value === "0" || value === "男")
            return 0
        if (value === "1" || value === "女")
            return 1
        return 2
    }

    function sexText() {
        switch (dialog.sexValue()) {
            case 0:
                return "男"
            case 1:
                return "女"
            default:
                return "未知"
        }
    }

    /// 申请状态文案。由控制器在 data 里算好（stateText）；缺了就退回原始编号
    function requestStateText() {
        if (!dialog.hasRequest)
            return ""
        const info = dialog.requestInfo
        return info.stateText !== undefined ? info.stateText : "STATE " + info.state
    }

    /// 只读态的字段表。备注只对"他人"有意义 —— 看自己的资料不存在备注
    function infoRows() {
        const rows = []

        // 已有申请时，先把这条申请本身摆出来 —— 这才是这一屏要看的东西
        if (dialog.hasRequest) {
            rows.push({
                label: "方向",
                value: dialog.requestInfo.received ? "收到的申请" : "我发出的申请"
            })
            rows.push({label: "状态", value: dialog.requestStateText()})
            rows.push({
                label: "验证信息",
                value: dialog.requestInfo.verifyInfo !== undefined
                    ? dialog.requestInfo.verifyInfo : ""
            })
        }

        /*
         * 备注只在**申请详情**里占一行。
         *
         * 服务端只给"我发出的申请"填这个字段（申请人自己给对面起的名字），
         * 收到的申请那侧恒为空 —— 空着还占一行"备注 —"，会被误读成
         * "这里本该有东西但丢了"，而且很容易和上面的「验证信息」看成同一个格子。
         *
         * 已经是好友的时候不走这里：那一行要带「修改」按钮（见下面的 remarkBlock）。
         */
        if (!dialog.isSelf && dialog.hasRequest && dialog.nickName.length > 0)
            rows.push({label: "备注", value: dialog.nickName})
        rows.push({label: "性别", value: dialog.sexText()})
        rows.push({label: "签名", value: dialog.desc})
        return rows
    }

    function primaryText() {
        if (dialog.editing)
            return dialog.busy ? "保存中…" : "保存"

        if (dialog.hasRequest) {
            // 这条申请已经有结果了 → 主按钮只是个"状态牌"，不能点
            const received = dialog.requestInfo.received === true
            switch (Number(dialog.requestInfo.state)) {
                case 1:
                    return received ? "已接受" : "对方已接受"
                case 2:
                    return received ? "已拒绝" : "对方已拒绝"
                default:
                    return received ? "待处理" : "申请已发送"
            }
        }

        if (dialog.isSelf)
            return "编辑资料"

        // 已经是好友：主按钮只是状态牌（点它发申请只会被服务端顶回 1016）
        if (dialog.isFriend)
            return "已是好友"

        return dialog.busy ? "提交中…" : "加好友"
    }

    function primaryAction() {
        // 已经有申请在，就别再发一次 —— 服务端只会回"重复申请"类的错误码
        if (dialog.hasRequest)
            return
        if (dialog.editing)
            dialog.submitProfile()
        else if (dialog.isSelf)
            dialog.enterEdit()
        else if (!dialog.isFriend)
            dialog.sendFriendRequest()
    }

    /// 进编辑态：把当前展示值灌进表单控件
    function enterEdit() {
        if (!dialog.isSelf)
            return

        const value = dialog.sexValue()
        dialog.editSex = value
        nameEdit.text = dialog.userName
        descEdit.text = dialog.desc
        sexMale.checked = value === 0
        sexFemale.checked = value === 1
        sexUnknown.checked = value === 2
        dialog.hint = ""
        dialog.hintIsError = false
        dialog.editing = true
    }

    function exitEdit() {
        dialog.editing = false
        dialog.hint = ""
        dialog.hintIsError = false
    }

    /// 点头像：没在编辑就进编辑，正在编辑就去换头像
    function toggleEdit() {
        if (dialog.editing)
            dialog.requestAvatarUpload()
        else
            dialog.enterEdit()
    }

    function sendFriendRequest() {
        if (dialog.busy || dialog.isSelf || dialog.isFriend)
            return
        dialog.friendRequestRequested(dialog.userId, dialog.verifyInfo, dialog.remark)
    }

    /*!
        接受 / 拒绝当前这条申请。

        只抛信号，不做任何网络判断 —— 提交、busy 态、结果提示都在页面与控制器那侧，
        这样弹窗不需要知道协议（state 的 1 / 2 只是转发）。
    */
    function authRequest(state) {
        if (dialog.busy || !dialog.canAuth)
            return
        dialog.friendAuthRequested(String(dialog.requestInfo.id), state)
    }

    /*!
        删除好友：两段式确认。

        第一次点只把按钮换成「确认删除」，第二次才真的抛信号；4 秒不点自动复位。
        删除在服务端是**双向、不可撤销**的（连备注一起删），所以这里不做"一点就走"。
    */
    function requestDeleteFriend() {
        if (dialog.busy || dialog.isSelf || dialog.hasRequest || !dialog.isFriend)
            return

        if (!dialog.deleteArmed) {
            dialog.deleteArmed = true
            dialog.showHint(dialog.deleteHintText, true)
            deleteArmTimer.restart()
            return
        }

        deleteArmTimer.stop()
        dialog.deleteArmed = false
        // 只抛信号：发请求、关窗、结果提示都在页面那侧
        dialog.friendDeleteRequested(dialog.userId)
    }

    /// 复位删除确认（Timer 到点 / 弹窗关闭）
    function resetDeleteArmed() {
        if (!dialog.deleteArmed)
            return

        dialog.deleteArmed = false
        // 只清自己写的那句，别把服务端刚回来的结果盖掉
        if (dialog.hint === dialog.deleteHintText)
            dialog.showHint("", false)
    }

    /*!
        开始改备注：把当前备注灌进输入框（单向赋值，不建立绑定）。

        只有「已经是好友的别人」才有这个动作 —— 非好友服务端会回 1022，
        申请详情里改的是申请记录上的备注，不是 `user_friend.remark`。
    */
    function beginEditRemark() {
        if (!dialog.isFriend || dialog.isSelf || dialog.hasRequest || dialog.busy)
            return

        dialog.remarkDraft = dialog.nickName
        remarkField.text = dialog.nickName
        dialog.remarkEditing = true
        // 每个账号的备注都只对自己可见，第一次点的人多半会担心对方看到
        dialog.showHint("备注只对自己可见，对方不会收到任何提示", false)
    }

    function cancelRemarkEdit() {
        dialog.remarkEditing = false
        dialog.remarkDraft = ""
        dialog.showHint("", false)
    }

    /*!
        提交备注（1029）。只抛信号，网络与结果提示都在页面 / 控制器那侧。
        空串是有意义的输入 —— 表示清空备注。
    */
    function submitRemark() {
        if (dialog.busy || !dialog.isFriend || !dialog.remarkEditing)
            return

        const next = String(remarkField.text).trim()
        if (next.length > 64) {
            dialog.showHint("备注最多 64 个字符", true)
            return
        }

        dialog.remarkDraft = next
        dialog.friendRemarkRequested(dialog.userId, next)
    }

    /// 页面在 1030 成功后调用：定稿备注并退出编辑态
    function applyRemark(remark) {
        dialog.nickName = remark
        dialog.remarkEditing = false
        dialog.remarkDraft = ""
        dialog.showHint(remark.length > 0 ? "备注已更新" : "已清除备注", false)
    }

    /// 页面在 1032 成功后调用：把新值写回展示数据并退出编辑态
    function applyProfile(name, sex, desc) {
        dialog.userName = name
        dialog.sex = sex
        dialog.desc = desc
        dialog.editing = false
        dialog.showHint("资料已保存", false)
    }

    function submitProfile() {
        if (dialog.busy || !dialog.isSelf)
            return

        // 服务端不校验昵称，空串会被原样写进库（好友列表里这个人就没名字了）
        const name = String(nameEdit.text).trim()
        if (name.length === 0) {
            dialog.showHint("昵称不能为空", true)
            return
        }

        dialog.profileSaveRequested(name, dialog.editSex, String(descEdit.text).trim())
    }

    /// 预留接口：请求上传头像。只抛信号，提示由页面给（页面才知道接口有没有接上）
    function requestAvatarUpload() {
        dialog.avatarUploadRequested()
    }

    /*!
        由页面调用：把服务端返回的提示写进底栏。

        \note 故意做成函数而不是属性绑定 —— 绑定一旦被赋值就永久断开，
              而这里需要"平时跟着控制器走、临时也能写一句本地提示"。
    */
    function showHint(text, isError) {
        dialog.hint = text
        dialog.hintIsError = isError === true
    }
}
