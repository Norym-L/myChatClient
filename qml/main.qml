import QtQuick
import QtQuick.Window
import ZmdUI 1.0

import MyChat.Core 1.0
import "pages"

/*!
    应用窗口壳（对应旧前端的 MainWindow —— 承担「路由容器」职责）。

    旧前端用 setCentralWidget + deleteLater 做一级路由；QML 这边用 Loader
    切换 sourceComponent 更自然，且天然避免了「旧页面继续响应全局信号」
    那个坑 —— Loader 卸载即断开连接。

    路由由 LoginController.loginSucceeded 驱动：只有整条链路
    （HTTP 登录 → 连接 ChatServer → TCP 登录握手）全部成功才切到聊天页。
*/
Window {
    id: app

    width: 960
    height: 660
    minimumWidth: 760
    minimumHeight: 560
    visible: true
    title: "MyChat"
    color: Theme.bg

    Loader {
        id: pageLoader
        anchors.fill: parent
        sourceComponent: loginPageComponent
    }

    Component {
        id: loginPageComponent
        LoginPage {
            onRegisterRequested: console.log("[qtClient] 跳转注册页（未实现）")
            onResetRequested: console.log("[qtClient] 跳转重置密码页（未实现）")
        }
    }

    Component {
        id: chatShellComponent
        ChatShellPage {
        }
    }

    Connections {
        target: LoginController

        function onLoginSucceeded() {
            pageLoader.sourceComponent = chatShellComponent
        }
    }
}
