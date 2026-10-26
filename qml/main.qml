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

    注册成功时服务端下发的 uid 由 RegisterController 暂存，登录页创建时
    用 takeRegisteredUid() 取走，回填账号框并显示「注册成功」横幅。

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
     * 自动化：登录页的账号卡片操作（MYCHAT_PICKER）。
     *
     * 取值 `select:<uid>` / `enter:<uid>`，由 main.cpp 从环境变量注入。
     * 卡片是点击驱动的，离屏抓图点不了鼠标 —— 没有这个开关就只能靠人眼看。
     */
    property string autoPick: ""

    /**
     * 自动搜索关键字（自动化验证用）。配合 MYCHAT_AUTOLOGIN + MYCHAT_SMOKE：
     * 登录成功进入聊天页后，自动切到「联系人」分区并发起一次搜索，
     * 用于无头验证搜索结果列表（尤其是多结果）的渲染。
     * 由 main.cpp 从环境变量 MYCHAT_SEARCH 注入，正常启动时为空。
     */
    property string autoSearch: ""

    /**
     * 自动打开用户资料弹窗（自动化验证用）。配合 MYCHAT_PAGE=Chat + MYCHAT_SMOKE：
     * 弹窗是 Popup，离屏抓图默认抓不到它，得让它自己弹出来才留得下证据。
     * 取值："other"（他人资料卡）/ "self"（自己的资料卡）/ "selfedit"（编辑态），
     * 由 main.cpp 从环境变量 MYCHAT_OPENDLG 注入，正常启动时为空。
     */
    property string autoDialog: ""

    /**
     * 自动发一条消息（自动化验证用）。进聊天页后替用户把这句话打进输入框并发送，
     * 走的是「输入框 → ChatController.sendText → 本地回显」这条真实链路。
     * 由 main.cpp 从环境变量 MYCHAT_SENDMSG 注入，正常启动时为空。
     */
    property string autoMessage: ""

    /**
     * 自动处理一条收到的好友申请（自动化验证用）：`accept` / `refuse`。
     * 进聊天页后切到「好友申请」分区，挑第一条「收到 + 等待处理」的申请，
     * 打开资料卡并替用户点「接受 / 拒绝」。由 main.cpp 从 MYCHAT_AUTHAPPLY 注入。
     */
    property string autoAuth: ""

    /**
     * 自动建群（自动化验证用）：`<群名>:<uid1,uid2>`，成员可省略。
     * 进聊天页后走真实的 1033 → 1035 链路建群，用来无头验证群会话 / 群消息。
     * 由 main.cpp 从环境变量 MYCHAT_GROUPCREATE 注入，正常启动时为空。
     */
    property string autoGroup: ""

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
            autoPick: app.autoPick
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
            // 窗口在前台？后台收到的消息不算已读，攒成红点（见 ChatPage.syncAttention）
            windowActive: app.active

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
