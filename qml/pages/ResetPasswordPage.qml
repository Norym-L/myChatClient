import QtQuick
import ZmdUI 1.0

import MyChat.Core 1.0
import "../components"

/*!
    重置密码页 —— 用 ZmdUI 组件重构旧前端的 resetdialog.ui。

    两步流程，全部交给 ResetPasswordController：
      · 点「获取验证码」→ requestVerifyCode(email)
      · 点「重 置」    → resetPassword(uid, email, newPassword, code)

    ⚠ 与注册的差别：这里要额外填 **uid**。服务端靠 (uid, email) 配对校验，
    只给邮箱是不够的。

    校验规则在 C++ 的 AccountValidator 里唯一实现 —— 本页不写正则。
*/
Item {
    id: page

    /*! 返回登录页 */
    signal backRequested()

    function clearInvalid() {
        uidEdit.invalid = false
        emailEdit.invalid = false
        pwdEdit.invalid = false
        codeEdit.invalid = false
    }

    function doRequestCode() {
        ResetPasswordController.requestVerifyCode(emailEdit.text)
    }

    function doReset() {
        page.clearInvalid()
        ResetPasswordController.resetPassword(uidEdit.text, emailEdit.text,
            pwdEdit.text, codeEdit.text)
    }

    // 重置成功后停留一下再回登录页
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

    // ── 卡片 ─────────────────────────────────────────────────
    ZPanel {
        id: card
        anchors.centerIn: parent
        width: 460

        title: "MyChat"
        eyebrow: "MESSAGE TERMINAL / BUILD 0.1"
        subtitle: "重置密码"
        tag: "AUTH-03"
        footerText: "GATE SERVER LINK"

        ZTextField {
            id: uidEdit
            width: parent.width
            label: "ACCOUNT"
            unit: "UID"
            placeholderText: "请输入账号（uid）"
            enabled: !ResetPasswordController.busy
            onTextChanged: invalid = false
        }

        ZTextField {
            id: emailEdit
            width: parent.width
            label: "EMAIL"
            unit: "MAIL"
            placeholderText: "该账号绑定的邮箱"
            enabled: !ResetPasswordController.busy
            onTextChanged: invalid = false
        }

        ZTextField {
            id: pwdEdit
            width: parent.width
            label: "NEW PASSWORD"
            unit: "PWD"
            placeholderText: "6~15 位，字母 / 数字 / !@#$%^&*"
            echoMode: TextInput.Password
            enabled: !ResetPasswordController.busy
            onTextChanged: invalid = false
        }

        // ── 验证码行 ──
        Row {
            width: parent.width
            spacing: Theme.spacingSm

            ZTextField {
                id: codeEdit
                width: parent.width - codeBtn.width - parent.spacing
                label: "VERIFY CODE"
                unit: "6DIG"
                placeholderText: "6 位验证码"
                enabled: !ResetPasswordController.busy
                onAccepted: page.doReset()
                onTextChanged: invalid = false
            }

            ZButton {
                id: codeBtn
                anchors.bottom: codeEdit.bottom
                text: ResetPasswordController.cooldown > 0
                    ? (ResetPasswordController.cooldown + "s")
                    : "获取验证码"
                variant: Theme.Variant.Secondary
                enabled: !ResetPasswordController.busy && ResetPasswordController.cooldown === 0
                onClicked: page.doRequestCode()
            }
        }

        // ── 提示行 ──
        Text {
            width: parent.width
            visible: ResetPasswordController.tip.length > 0
            text: ResetPasswordController.tip
            font: Theme.fontBody
            color: ResetPasswordController.tipIsError ? Theme.danger : Theme.muted
            wrapMode: Text.WordWrap
        }

        // ── 操作区 ──
        Row {
            spacing: Theme.spacingMd

            ZButton {
                text: ResetPasswordController.busy ? "提交中…" : "重 置"
                variant: Theme.Variant.Primary
                mono: "ENTER"
                enabled: !ResetPasswordController.busy
                onClicked: page.doReset()
            }

            ZButton {
                text: "返回登录"
                variant: Theme.Variant.Ghost
                enabled: !ResetPasswordController.busy
                onClicked: page.backRequested()
            }
        }
    }

    // 本地校验失败 → 高亮并聚焦出错的输入框
    Connections {
        target: ResetPasswordController

        function onValidationFailed(field) {
            if (field === "uid") {
                uidEdit.invalid = true
                uidEdit.forceActiveFocus()
            } else if (field === "email") {
                emailEdit.invalid = true
                emailEdit.forceActiveFocus()
            } else if (field === "password") {
                pwdEdit.invalid = true
                pwdEdit.forceActiveFocus()
            } else if (field === "code") {
                codeEdit.invalid = true
                codeEdit.forceActiveFocus()
            }
        }

        function onResetSucceeded() {
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
