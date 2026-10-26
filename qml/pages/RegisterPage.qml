import QtQuick
import ZmdUI 1.0

import MyChat.Core 1.0
import "../components"

/*!
    注册页 —— 用 ZmdUI 组件重构旧前端的 registerdialog.ui。

    两步流程，全部交给 RegisterController：
      · 点「获取验证码」→ requestVerifyCode(email)，随后按钮进入 60 秒冷却
      · 点「注 册」    → registerUser(email, nickname, password, confirm, code)

    校验规则在 C++ 的 AccountValidator 里唯一实现 —— 本页不写正则。
    本页只负责「收集输入 + 显示状态」，不含任何协议细节。
*/
Item {
    id: page

    /*! 返回登录页（注册成功拿到的 uid 由 RegisterController 暂存，登录页会取走） */
    signal backRequested()

    /// 清掉所有输入框的出错高亮
    function clearInvalid() {
        emailEdit.invalid = false
        nickEdit.invalid = false
        pwdEdit.invalid = false
        confirmEdit.invalid = false
        codeEdit.invalid = false
    }

    function doRequestCode() {
        RegisterController.requestVerifyCode(emailEdit.text)
    }

    function doRegister() {
        page.clearInvalid()
        RegisterController.registerUser(emailEdit.text, nickEdit.text,
            pwdEdit.text, confirmEdit.text,
            codeEdit.text)
    }

    // 注册成功后停留一下再回登录页（uid 已由 RegisterController 暂存，登录页接着显示）
    Timer {
        id: backTimer
        interval: 2000
        repeat: false
        onTriggered: page.backRequested()
    }

    // ── 工程图纸网格铺底 ──────────────────────────────────────
    ZGridBackground {
        anchors.fill: parent
        bgColor: Theme.bg
        minorColor: Theme.gridLine
        majorColor: Theme.gridLineMajor
        crossColor: Theme.lineSoft
    }

    // ── 注册卡片 ─────────────────────────────────────────────
    ZPanel {
        id: card
        anchors.centerIn: parent
        width: 460

        title: "MyChat"
        eyebrow: "MESSAGE TERMINAL / BUILD 0.1"
        subtitle: "注册新账号"
        tag: "AUTH-02"
        footerText: "GATE SERVER LINK"

        ZTextField {
            id: emailEdit
            width: parent.width
            label: "EMAIL"
            unit: "MAIL"
            placeholderText: "请输入邮箱"
            enabled: !RegisterController.busy
            onTextChanged: invalid = false
        }

        ZTextField {
            id: nickEdit
            width: parent.width
            label: "NICKNAME"
            unit: "NAME"
            placeholderText: "2~16 位昵称"
            enabled: !RegisterController.busy
            onTextChanged: invalid = false
        }

        ZTextField {
            id: pwdEdit
            width: parent.width
            label: "PASSWORD"
            unit: "PWD"
            placeholderText: "6~15 位，字母 / 数字 / !@#$%^&*"
            echoMode: TextInput.Password
            enabled: !RegisterController.busy
            onTextChanged: invalid = false
        }

        ZTextField {
            id: confirmEdit
            width: parent.width
            label: "CONFIRM"
            unit: "PWD"
            placeholderText: "请再输入一次密码"
            echoMode: TextInput.Password
            enabled: !RegisterController.busy
            onAccepted: page.doRegister()
            onTextChanged: invalid = false
        }

        // ── 验证码行：输入框 + 带冷却的获取按钮 ──
        Row {
            width: parent.width
            spacing: Theme.spacingSm

            ZTextField {
                id: codeEdit
                width: parent.width - codeBtn.width - parent.spacing
                label: "VERIFY CODE"
                unit: "6DIG"
                placeholderText: "6 位验证码"
                enabled: !RegisterController.busy
                onTextChanged: invalid = false
            }

            // 按钮底部与输入框对齐：ZTextField 比按钮高，这里往下推一点
            ZButton {
                id: codeBtn
                anchors.bottom: codeEdit.bottom
                text: RegisterController.cooldown > 0
                    ? (RegisterController.cooldown + "s")
                    : "获取验证码"
                variant: Theme.Variant.Secondary
                enabled: !RegisterController.busy && RegisterController.cooldown === 0
                onClicked: page.doRequestCode()
            }
        }

        // ── 提示行：错误用危险色，过程提示用普通色 ──
        // 不要写 `height: visible ? implicitHeight : 0` —— 会与 implicitHeight 形成绑定循环；
        // Column 本来就会跳过 visible=false 的子项。
        Text {
            width: parent.width
            visible: RegisterController.tip.length > 0
            text: RegisterController.tip
            font: Theme.fontBody
            color: RegisterController.tipIsError ? Theme.danger : Theme.muted
            wrapMode: Text.WordWrap
        }

        // ── 操作区 ──
        Row {
            spacing: Theme.spacingMd

            ZButton {
                text: RegisterController.busy ? "提交中…" : "注 册"
                variant: Theme.Variant.Primary
                mono: "ENTER"
                enabled: !RegisterController.busy
                onClicked: page.doRegister()
            }

            ZButton {
                text: "返回登录"
                variant: Theme.Variant.Ghost
                enabled: !RegisterController.busy
                onClicked: page.backRequested()
            }
        }
    }

    // 本地校验失败 → 高亮并聚焦出错的输入框
    Connections {
        target: RegisterController

        function onValidationFailed(field) {
            if (field === "email") {
                emailEdit.invalid = true
                emailEdit.forceActiveFocus()
            } else if (field === "nickname") {
                nickEdit.invalid = true
                nickEdit.forceActiveFocus()
            } else if (field === "password") {
                pwdEdit.invalid = true
                pwdEdit.forceActiveFocus()
            } else if (field === "confirm") {
                confirmEdit.invalid = true
                confirmEdit.forceActiveFocus()
            } else if (field === "code") {
                codeEdit.invalid = true
                codeEdit.forceActiveFocus()
            }
        }

        // 注册成功：服务端下发的 uid 由控制器暂存（登录页会 take 走并回填），
        // 这里让「注册成功」的提示停留一会儿再自动回登录页 —— 直接切走的话
        // 用户根本来不及看清自己刚拿到的账号。
        function onRegisterSucceeded(uid) {
            backTimer.restart()
        }
    }

    // ── 左下角：主题切换（和登录页同一个组件，位置也一致）──────
    ThemeToggle {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacingLg
    }

}
