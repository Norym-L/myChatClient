import QtQuick
import QtQuick.Controls.Basic

/*!
    \qmltype ZTextField
    \inqmlmodule ZmdUI
    \brief 输入框：黑色硬边 + 右上 / 右下斜切角，聚焦时亮黄底线；光标只在编辑时出现并闪烁。

    标签（\l label）以等宽小字嵌在框内左上角，\l unit 显示在右侧，
    这是工业面板上常见的"字段铭牌"排布，不需要额外的外部标签占位。

    \qml
    ZTextField {
        width: 320
        label: "BOT ID"
        placeholderText: "bot 1 - 36 - · · · - 007"
        unit: "ID"
    }
    \endqml
*/
TextField {
    id: control

    /*! 左侧 / 上方的字段铭牌，空则隐藏 */
    property string label: ""
    /*! 右侧单位 / 后缀 */
    property string unit: ""
    /*! 是否出错（底线转品红） */
    property bool invalid: false

    implicitHeight: Theme.dp(label.length > 0 ? Theme.controlHeightLg : Theme.controlHeight)
    leftPadding: Theme.spacingMd
    rightPadding: Theme.spacingMd + (unit.length > 0 ? unitText.implicitWidth + Theme.spacingSm : 0)
    topPadding: label.length > 0 ? Theme.spacingLg : Theme.spacingSm
    bottomPadding: Theme.spacingSm

    color: enabled ? Theme.ink : Theme.muted
    font: Theme.fontBody
    placeholderTextColor: Theme.muted
    selectionColor: Theme.accent
    selectedTextColor: Theme.textOnAccent
    verticalAlignment: TextInput.AlignVCenter
    selectByMouse: true

    // 光标（caret）交给系统默认实现会偏灰，这里自定义。
    //
    // ⚠️ 注意：自定义 cursorDelegate 之后，**Qt 不再帮你管可见性和闪烁** ——
    //    这两件事必须由 delegate 自己实现（Qt 自己的 Material 风格也是这么写的）。
    //    之前只给了宽高和颜色，结果光标一旦出现就永远亮着、而且从不闪烁。
    cursorDelegate: Rectangle {
        id: caret
        width: 2
        color: Theme.ink
        // 只在"真的在编辑"时出现：有焦点、非只读、且没有选区
        // （有选区时该显示选区高亮，不该显示插入符）
        visible: control.activeFocus && !control.readOnly
                 && control.selectionStart === control.selectionEnd

        // 光标位置一动就重新亮起并重置计时：打字时不该正好撞上"熄灭"那一拍
        Connections {
            target: control
            function onCursorPositionChanged() {
                caret.opacity = 1
                caretBlink.restart()
            }
        }

        // 闪烁节奏跟随系统设置（见 Theme.caretFlashHalf）
        Timer {
            id: caretBlink
            running: caret.visible
            repeat: true
            interval: Theme.caretFlashHalf
            onTriggered: caret.opacity = caret.opacity > 0.5 ? 0 : 1
            // 一拿到焦点先亮起来，不要从"暗"那一拍开始
            onRunningChanged: caret.opacity = 1
        }
    }

    background: Item {
        ZCutRect {
            anchors.fill: parent
            color: control.enabled ? Theme.surface : Theme.surfaceAlt
            strokeColor: control.invalid ? Theme.magenta : Theme.line
            strokeWidth: Theme.border
            cutBottomRight: Theme.cutMd
            cutTopLeft: Theme.cutSm
        }

        // ── 底线：默认细灰线，聚焦/出错时变成粗亮黄（或品红）线 ──
        Rectangle {
            id: underline
            x: Theme.border / 2
            y: parent.height - height - Theme.border / 2
            width: parent.width - Theme.cutMd - Theme.border
            height: control.activeFocus ? 4 : (control.hovered ? 3 : 2)
            color: control.invalid ? Theme.magenta
                 : control.activeFocus ? Theme.accent
                 : Theme.lineSoft

            Behavior on height {
                NumberAnimation { duration: Theme.durFast; easing.type: Theme.easingStd }
            }
        }

        // ── 字段铭牌 ──
        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingMd
            anchors.top: parent.top
            anchors.topMargin: 5
            visible: control.label.length > 0
            text: control.label
            font: Theme.fontMonoSm
            color: control.activeFocus ? Theme.ink : Theme.muted
        }

        // ── 右侧单位块：黑底白字小队标 ──
        ZCutRect {
            id: unitBadge
            anchors.right: parent.right
            anchors.rightMargin: Theme.cutMd + 4
            anchors.verticalCenter: parent.verticalCenter
            width: unitText.implicitWidth + Theme.spacingMd
            height: 18
            visible: control.unit.length > 0
            color: Theme.subBg
            cutTopRight: 5

            Text {
                id: unitText
                anchors.centerIn: parent
                text: control.unit
                font: Theme.fontMonoSm
                color: Theme.textOnSub
            }
        }

        // ── 聚焦时的四角标记（工程瞄准框的感觉）──
        ZCutRect {
            anchors.fill: parent
            anchors.margins: -3
            visible: control.activeFocus
            color: "transparent"
            strokeColor: Theme.ink
            strokeWidth: 1
            cutBottomRight: Theme.cutMd + 3
            cutTopLeft: Theme.cutSm + 3
            opacity: 0.35
        }
    }
}
