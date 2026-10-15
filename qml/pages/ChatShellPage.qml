import QtQuick
import ZmdUI 1.0

import MyChat.Core 1.0

/*!
    聊天主页空壳 —— 本阶段只用来验证「登录链路打通后能切过来」。

    会话列表、消息气泡、联系人页等按计划在后续阶段用 ZmdUI 逐页替换。
    这里刻意保留 UserManager 的登录结果展示，好确认 C++ 侧状态确实传到了 QML。
*/
Item {
    id: page

    ZGridBackground {
        anchors.fill: parent
        bgColor: Theme.bg
        minorColor: Theme.gridLine
        majorColor: Theme.gridLineMajor
        crossColor: Theme.lineSoft
    }

    Column {
        anchors.centerIn: parent
        spacing: Theme.spacingLg

        ZPanel {
            width: 520

            title: "已登录"
            eyebrow: "SESSION ESTABLISHED"
            subtitle: "聊天主页占位"
            tag: "SHELL-01"
            footerText: "CHAT SERVER CONNECTED"

            Text {
                width: parent.width
                text: "登录链路已打通：HTTP 登录 → 连接 ChatServer → TCP 登录握手。"
                font: Theme.fontBody
                color: Theme.inkSoft
                wrapMode: Text.WordWrap
            }

            // 登录结果（来自 C++ 的 UserManager）
            Row {
                spacing: Theme.spacingSm

                Rectangle {
                    width: 8
                    height: 8
                    color: Theme.accent
                    anchors.verticalCenter: parent.verticalCenter
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: LoginController.userLabel
                    font: Theme.fontMonoMd
                    color: Theme.ink
                }
            }

            Text {
                width: parent.width
                text: "会话列表、消息气泡、联系人页将在后续阶段用 ZmdUI 逐页替换。"
                font: Theme.fontBody
                color: Theme.muted
                wrapMode: Text.WordWrap
            }
        }
    }
}
