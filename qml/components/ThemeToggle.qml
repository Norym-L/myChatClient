import QtQuick
import QtCore
import ZmdUI 1.0

/*!
    主题切换（亮色 / 暗色）。

    改的就是 `Theme.mode` 这一个值：所有组件的颜色都是从 `Theme` 令牌绑过来的，
    不需要逐个 notify，改一下整界面立刻换皮。

    \section1 为什么配一个 Settings

    不记住上次的选择的话，每次开程序都回到浅色，等于让用户"每天点一次"。
    存在 QSettings 里（`%APPDATA%/MyChat/MyChat.ini` 的 `[Theme]`），键是 `mode`。

    \note 目前只有 ChatPage / LoginPage 各放一份实例。两个实例各自持有一个
          Settings 对象，但它们读写的是**同一个键**，且显示状态取自 `Theme.isDark`
          （不是各自的副本）→ 不会出现"A 切了 B 还显示旧的"。

    \section1 放在哪

    `ZSideBar` 的 default property 是底部页脚，所以直接作为它的子项即可挂在左下角：

    \code
    ZSideBar {
        ThemeToggle { width: parent.width - Theme.spacingLg - Theme.spacingMd }
    }
    \endcode
*/
Column {
    id: root

    spacing: Theme.spacingSm

    /*!
        恢复上次的选择。`Settings` 在构造时已经把磁盘上的值读进 `store.mode`，
        所以这里不能再晚一步 —— 放 Component.onCompleted 之前界面会闪一下浅色。
    */
    Component.onCompleted: root.apply(themeStore.mode)

    /// 写回并落盘。mode 只接受 Theme.Mode 的两个值，其它一律忽略。
    function apply(mode) {
        if (mode !== Theme.Mode.Light && mode !== Theme.Mode.Dark)
            return
        themeStore.mode = mode
        Theme.mode = mode
    }

    function toggle() {
        root.apply(Theme.isDark ? Theme.Mode.Light : Theme.Mode.Dark)
    }

    Settings {
        id: themeStore
        category: "Theme"
        property int mode: Theme.Mode.Light
    }

    // 侧边栏那条深色带上的辅助色（品牌区下方用的是 #8C8C8C，这里保持一致）
    readonly property color idleText: "#8C8C8C"
    readonly property color idleFill: Theme.isDark ? "#161616" : "#1C1C1C"

    // ── 小标题：和导航里的分组标题同一套写法（短黄杠 + 等宽字）──────
    Row {
        spacing: Theme.spacingSm

        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 12
            height: 2
            color: Theme.accent
            opacity: 0.7
        }

        Text {
            text: "主题 / THEME"
            font: Theme.fontMonoSm
            color: root.idleText
        }
    }

    // ── 分档按钮 ──────────────────────────────────────────────
    Row {
        id: chips
        spacing: Theme.spacingSm

        Repeater {
            model: [
                {value: Theme.Mode.Light, label: "浅色"},
                {value: Theme.Mode.Dark, label: "暗色"}
            ]

            delegate: Item {
                id: chip

                required property var modelData

                width: 64
                height: 28

                readonly property bool active: Theme.mode === modelData.value

                ZCutRect {
                    anchors.fill: parent
                    color: chip.active ? Theme.accent : root.idleFill
                    strokeColor: chip.active ? Theme.accent : Theme.lineSoft
                    strokeWidth: 1
                    cutTopRight: 6
                    cutBottomLeft: 6
                }

                Text {
                    anchors.centerIn: parent
                    text: modelData.label
                    font: Theme.fontStrong
                    color: chip.active ? Theme.textOnAccent : "#FFFFFF"
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.apply(modelData.value)
                }
            }
        }
    }
}
