import QtQuick
import ZmdUI 1.0

import MyChat.Core 1.0
import "../components"

/*!
    登录页 —— 用 ZmdUI 组件重构旧前端的 logindialog.ui。

    校验与网络全部交给 LoginController：
      · 输入校验用 checkAccount / checkPassword（正则只有 C++ 那一份，避免写歪）
      · 提交调 login()；过程提示读 LoginController.tip，忙碌态读 busy
      · 全链路成功由 LoginController.loginSucceeded 通知，由窗口壳负责切页

    本页只负责「收集输入 + 显示状态」，不含任何协议细节。
*/
Item {
    id: page

    /*! 跳转注册页 */
        signal
    registerRequested

    /*! 跳转重置密码页 */
        signal
    resetRequested

    /*!
        自动化：进页面后替用户操作账号卡片（MYCHAT_PICKER）。

        取值：`select:<uid>` 单击选中某张卡 / `enter:<uid>` 双击免密进入。
        卡片是 QML 里点击驱动的，C++ 自检只能验证 controllers 那一半，
        "点一下到底有没有接上"只有跑真实页面才算数 —— 离屏又点不了鼠标。
    */
    property string autoPick: ""

    /*
     * main.cpp 是在 engine.load() **之后**才 setProperty 的，那时本页已经
     * Component.onCompleted 过了 —— 光在完成时判断会永远看不到这个值。
     * 两个入口都留着：先设后建（onCompleted）和前文这种后设（onChanged）。
     */
    onAutoPickChanged: {
        if (autoPick.length > 0)
            autoPickTimer.restart()
    }

    /// 提交登录：先清掉上一次的高亮，校验交给 C++
    function doLogin() {
        accountEdit.invalid = false;
        passEdit.invalid = false;
        LoginController.login(accountEdit.text, passEdit.text);
    }

    /**
     * 当前选中的那条账号记录（null = 没选 / 选中的号已被移除）。
     *
     * 写成 property 而不是函数：函数调用不会建立绑定依赖，accounts 变了
     * 按钮文案不会跟着变。这里显式读两个属性，两个信号都能触发重算。
     */
    readonly property var selectedEntry: {
        const list = LoginController.accounts
        const uid = LoginController.selectedUid
        for (let i = 0; i < list.length; ++i) {
            if (String(list[i].uid) === uid)
                return list[i]
        }
        return null
    }

    /// 选中的账号有可用凭据、且账号框没被改成别的号 → 可以直接免密进
    readonly property bool savedCredentialReady: selectedEntry !== null
        && selectedEntry.hasToken === true
        && accountEdit.text === LoginController.selectedUid

    /// 主按钮走「免密进入」：还要密码框空着（用户填了密码就是想用密码登）
    readonly property bool primaryIsSaved: savedCredentialReady
        && passEdit.text.length === 0

    /// 单击卡片：第一次选中，第二次直接进（真实点击和自动化都走这里）
    function handleCardClick(uid, hasToken) {
        if (LoginController.selectedUid === uid && hasToken)
            page.enterAccount(uid)
        else
            page.pickAccount(uid)
    }

    /// 按 uid 找账号记录（找不到返回 null）
    function entryFor(uid) {
        const list = LoginController.accounts
        for (let i = 0; i < list.length; ++i) {
            if (String(list[i].uid) === uid)
                return list[i]
        }
        return null
    }

    /// 单击卡片：选中并把 uid 填进账号框（用户还能改，改了就按新号登录）
    function pickAccount(uid) {
        LoginController.selectAccount(uid)
        accountEdit.text = uid
        passEdit.text = ""
        accountEdit.invalid = false
        passEdit.invalid = false
    }

    /// 双击卡片：有凭据就直接进，没凭据就把光标送到密码框
    function enterAccount(uid) {
        page.pickAccount(uid)
        if (!LoginController.loginWithAccount(uid))
            passEdit.forceActiveFocus()
    }

    /// 「+」卡：切回空白表单（不删任何已记住的账号）
    function startNewAccount() {
        LoginController.selectAccount("")
        accountEdit.text = ""
        passEdit.text = ""
        accountEdit.forceActiveFocus()
    }

    /// 主按钮：能免密就免密，否则按密码登录
    function doPrimary() {
        if (page.primaryIsSaved && LoginController.loginWithAccount(accountEdit.text))
            return
        page.doLogin()
    }

    /**
     * 把选中的账号同步到输入框。
     *
     * `tryAutoLogin()` 在"只有一个账号 / 上次用的那个"时会把 selectedUid 设好，
     * 但那是 C++ 侧的状态，输入框不会自己变 —— 不同步的话用户看到一张高亮的卡、
     * 账号框却还是空的，还得自己再敲一遍 uid。
     */
    function syncSelectedAccount() {
        if (LoginController.selectedUid.length === 0)
            return
        if (accountEdit.text === LoginController.selectedUid)
            return
        accountEdit.text = LoginController.selectedUid
        passEdit.text = ""
    }

    Component.onCompleted: {
        page.syncSelectedAccount()
        if (page.autoPick.length > 0)
            autoPickTimer.restart()
    }

    /// 仅自动化用：MYCHAT_PICKER 的落地动作（见 autoPick 注释）
    Timer {
        id: autoPickTimer
        interval: 600
        onTriggered: {
            const parts = page.autoPick.split(":")
            const action = parts[0]
            const uid = parts.length > 1 ? parts[1] : ""
            if (uid.length === 0)
                return

            console.log("[login] 自动化：账号卡片", action, uid)
            if (action === "enter") {
                page.enterAccount(uid)
            } else if (action === "tap") {
                // 模拟"点两下"：走的是 delegate onClicked 的同一个函数
                const entry = page.entryFor(uid)
                const hasToken = entry !== null && entry.hasToken === true
                page.handleCardClick(uid, hasToken)
                page.handleCardClick(uid, hasToken)
            } else {
                page.pickAccount(uid)
            }
        }
    }

    // ── 工程图纸网格铺底 ──────────────────────────────────────
    ZGridBackground {
        anchors.fill: parent
        bgColor: Theme.bg
        minorColor: Theme.gridLine
        majorColor: Theme.gridLineMajor
        crossColor: Theme.lineSoft
    }

    // ── 登录卡片 ─────────────────────────────────────────────
    ZPanel {
        id: card
        anchors.centerIn: parent
        width: 460

        title: "MyChat"
        eyebrow: "MESSAGE TERMINAL / BUILD 0.1"
        subtitle: "账号登录"
        tag: "AUTH-01"
        footerText: "GATE SERVER LINK"

        /*
         * ── 账号卡片条（左右滑动选择）──────────────────────────────
         *
         * 只在本机记住过账号时出现。ListView 横向 + SnapToItem：滑动一次停到
         * 下一张卡上，不会停在半张的位置上 —— 手感和"翻卡"一致。
         *
         * 数据全部来自 LoginController.accounts（控制器从 session.ini 读），
         * 本页不碰存储，也不知道 token 存在哪。
         */
        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: LoginController.accounts.length > 0

            Row {
                width: parent.width
                spacing: Theme.spacingSm

                Text {
                    text: "选择账号"
                    font: Theme.fontMonoSm
                    color: Theme.muted
                }

                Text {
                    text: "左右滑动 · 单击选中 · 再点一次进入"
                    font: Theme.fontMonoSm
                    color: Theme.muted
                    opacity: 0.7
                }
            }

            Row {
                width: parent.width
                spacing: Theme.spacingSm

                // 包一层 Item：渐隐条要盖在列表上（同层），不能占 Row 的格子
                Item {
                    id: accountListWrap
                    width: parent.width - newAccountCard.width - parent.spacing
                    height: 88

                    ListView {
                        id: accountList
                        anchors.fill: parent
                        orientation: ListView.Horizontal
                        spacing: Theme.spacingSm
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        // 一张一张地停：SnapOneItem 按视口翻页，卡片宽度不等于视口，
                        // 会一次跨过两张；SnapToItem 才是"停在某张卡上"
                        snapMode: ListView.SnapToItem
                        model: LoginController.accounts

                        delegate: AccountCard {
                            required property var modelData

                            uid: String(modelData.uid)
                            name: modelData.name !== undefined ? modelData.name : ""
                            hasToken: modelData.hasToken === true
                            isLast: modelData.isLast === true
                            lastText: modelData.lastText !== undefined ? modelData.lastText : ""
                            selected: LoginController.selectedUid === String(modelData.uid)

                            // 第一次点选账号（露出「移除」），再点一次直接进 ——
                            // 不用非得双击，符合"点一下就走"的直觉
                            onClicked: page.handleCardClick(String(modelData.uid),
                                                            modelData.hasToken === true)
                            onActivated: page.enterAccount(String(modelData.uid))
                            onRemoveRequested: LoginController.forgetAccount(String(modelData.uid))
                        }
                    }

                    // 右侧渐隐：卡片多于两屏时最右那张是"切一半"的，
                    // 没有这层淡出会看着像布局被撑坏了
                    Rectangle {
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 26
                        visible: accountList.contentWidth > accountList.width
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop {
                                position: 0.0
                                color: Qt.rgba(Theme.surface.r, Theme.surface.g,
                                               Theme.surface.b, 0)
                            }
                            GradientStop { position: 1.0; color: Theme.surface }
                        }
                    }
                }

                // 「新账号」：切回空白表单（不删任何卡片）
                Rectangle {
                    id: newAccountCard
                    width: 44
                    height: 88
                    color: newMouse.containsMouse ? Theme.surfaceHover : Theme.surfaceMuted
                    border.width: 1
                    border.color: LoginController.selectedUid.length === 0
                                  ? Theme.accent : Theme.lineSoft

                    Text {
                        anchors.centerIn: parent
                        text: "+"
                        font: Theme.fontHeading
                        color: Theme.ink
                    }

                    MouseArea {
                        id: newMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: page.startNewAccount()
                    }
                }
            }
        }

        ZTextField {
            id: accountEdit
            width: parent.width
            label: "ACCOUNT"
            unit: "UID"
            placeholderText: "请输入账号"
            enabled: !LoginController.busy

            onAccepted: page.doPrimary()
            // 重新输入即清掉出错高亮，避免红框一直挂着
            onTextChanged: invalid = false
        }

        ZTextField {
            id: passEdit
            width: parent.width
            label: "PASSWORD"
            unit: "PWD"
            /*
             * 选了"记着登录状态"的账号时，密码框会空着 —— 空着但主按钮是「进入」，
             * 用户容易以为哪里没填。占位文案直接说明可以跳过，比塞一串假圆点诚实。
             */
            placeholderText: page.savedCredentialReady
                                 ? "已记住登录状态，点「进入」即可"
                                 : "请输入密码"
            echoMode: TextInput.Password
            enabled: !LoginController.busy

            onAccepted: page.doPrimary()
            onTextChanged: invalid = false
        }

        // ── 提示行：错误用危险色，过程提示用普通色 ──
        // 注意不要写 `height: visible ? implicitHeight : 0`：Text 的 implicitHeight
        // 依赖 width（wrapMode 会重排），再让 height 反过来依赖它就会触发绑定循环。
        // Column 本来就会跳过 visible=false 的子项，不需要手动把高度置 0。
        Text {
            width: parent.width
            visible: LoginController.tip.length > 0
            text: LoginController.tip
            font: Theme.fontBody
            color: LoginController.tipIsError ? Theme.danger : Theme.muted
            wrapMode: Text.WordWrap
        }

        // ── 操作区：登录过程中整体禁用，避免重复提交 ──
        Row {
            spacing: Theme.spacingMd

            ZButton {
                text: LoginController.busy ? "登录中…"
                                           : (page.primaryIsSaved ? "进 入" : "登 录")
                variant: Theme.Variant.Primary
                mono: "ENTER"
                enabled: !LoginController.busy
                onClicked: page.doPrimary()
            }

            ZButton {
                text: "注 册"
                variant: Theme.Variant.Secondary
                enabled: !LoginController.busy
                onClicked: page.registerRequested()
            }

            ZButton {
                text: "忘记密码"
                variant: Theme.Variant.Ghost
                enabled: !LoginController.busy
                onClicked: page.resetRequested()
            }
        }
    }

    // ── 左下角：主题切换（登录页没有侧边栏，直接锚在窗口左下）──────
    // 放在这里而不是仅在聊天页：登录是每次打开程序的第一屏，
    // 主题得在进主界面之前就能改，而且要看得见"上一次选的是哪个"。
    ThemeToggle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingLg
    }

    // 本地校验失败 → 高亮并聚焦出错的输入框
    Connections {
        target: LoginController

        function onSelectedUidChanged() {
            page.syncSelectedAccount()
        }

        function onValidationFailed(field) {
            if (field === "account") {
                accountEdit.invalid = true;
                accountEdit.forceActiveFocus();
            } else if (field === "password") {
                passEdit.invalid = true;
                passEdit.forceActiveFocus();
            }
        }
    }
}
