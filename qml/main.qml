import QtQuick
import QtQuick.Window
import ZmdUI 1.0

import MyChat.Core 1.0
import "pages"
import "components"

/*!
    应用窗口壳（对应旧前端的 MainWindow —— 承担「路由容器」职责）。

    旧前端用 setCentralWidget + deleteLater 做一级路由；QML 这边用 Loader
    切换 sourceComponent 更自然，且天然避免了「旧页面继续响应全局信号」
    那个坑 —— Loader 卸载即断开连接。

    路由表：
      LoginPage  ──loginSucceeded──▶  ChatPage  ──logoutRequested──▶ LoginPage
      LoginPage  ──registerRequested──▶  RegisterPage  ──backRequested──▶ LoginPage
      LoginPage  ──resetRequested──────▶  ResetPasswordPage ──backRequested──▶ LoginPage

    只有整条登录链路（HTTP 登录 → 连接 ChatServer → TCP 登录握手）全部成功
    才切到聊天页，其余跳转都是用户主动导航。
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

    /**
     * 起始页。正常就是登录页；设了环境变量 MYCHAT_PAGE 时由 main.cpp 覆盖，
     * 用来配合 MYCHAT_SMOKE 对任意一个页面做无头抓图自检。
     * 取值："Login"（默认）/ "Register" / "Reset" / "Chat"（大小写不敏感）。
     *
     * 注意 Chat 页在真实流程里是登录成功后才进的，直接跳过去只有 UI 壳，
     * 没有任何业务数据 —— 抓图自检够用，别拿它当功能验证。
     */
    property string startPage: "Login"

    /**
     * 自动搜索关键字（自动化验证用）。配合 MYCHAT_AUTOLOGIN + MYCHAT_SMOKE：
     * 登录成功进入聊天页后，自动切到「联系人」分区并发起一次搜索，
     * 用于无头验证搜索结果列表（尤其是多结果）的渲染。
     * 由 main.cpp 从环境变量 MYCHAT_SEARCH 注入，正常启动时为空。
     */
    property string autoSearch: ""

    Loader {
        id: pageLoader
        anchors.fill: parent
        sourceComponent: loginPageComponent
    }

    // 起始页覆盖：只认大写开头的三个名字，写错直接回落登录页并告警，
    // 免得静默起一个空白窗口让人以为是渲染坏了。
    function applyStartPage() {
        const p = app.startPage.toLowerCase()
        if (p === "register") {
            pageLoader.sourceComponent = registerPageComponent
        } else if (p === "reset") {
            pageLoader.sourceComponent = resetPageComponent
        } else if (p === "chat") {
            pageLoader.sourceComponent = chatPageComponent
        } else if (p !== "login") {
            console.warn("[qtClient] 未知的 MYCHAT_PAGE:", app.startPage, "-> 回落登录页")
        }
    }

    Component {
        id: loginPageComponent
        LoginPage {
            onRegisterRequested: pageLoader.sourceComponent = registerPageComponent
            onResetRequested: pageLoader.sourceComponent = resetPageComponent
        }
    }

    Component {
        id: registerPageComponent
        RegisterPage {
            onBackRequested: pageLoader.sourceComponent = loginPageComponent
        }
    }

    Component {
        id: resetPageComponent
        ResetPasswordPage {
            onBackRequested: pageLoader.sourceComponent = loginPageComponent
        }
    }

    Component {
        id: chatPageComponent
        ChatPage {
            // 退出登录：回到登录页并断开聊天服连接
            onLogoutRequested: {
                LoginController.logout()
                pageLoader.sourceComponent = loginPageComponent
            }
        }
    }

    Connections {
        target: LoginController

        function onLoginSucceeded() {
            pageLoader.sourceComponent = chatPageComponent
        }
    }

    Component.onCompleted: app.applyStartPage()
}
