import QtQuick
import ZmdUI 1.0

import MyChat.Core 1.0

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

    /// 提交登录：先清掉上一次的高亮，校验交给 C++
    function doLogin() {
        accountEdit.invalid = false;
        passEdit.invalid = false;
        LoginController.login(accountEdit.text, passEdit.text);
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

        ZTextField {
            id: accountEdit
            width: parent.width
            label: "ACCOUNT"
            unit: "UID"
            placeholderText: "请输入账号"
            enabled: !LoginController.busy

            onAccepted: page.doLogin()
            // 重新输入即清掉出错高亮，避免红框一直挂着
            onTextChanged: invalid = false
        }

        ZTextField {
            id: passEdit
            width: parent.width
            label: "PASSWORD"
            unit: "PWD"
            placeholderText: "请输入密码"
            echoMode: TextInput.Password
            enabled: !LoginController.busy

            onAccepted: page.doLogin()
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
                text: LoginController.busy ? "登录中…" : "登 录"
                variant: Theme.Variant.Primary
                mono: "ENTER"
                enabled: !LoginController.busy
                onClicked: page.doLogin()
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

    // 本地校验失败 → 高亮并聚焦出错的输入框
    Connections {
        target: LoginController

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
