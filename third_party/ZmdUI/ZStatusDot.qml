import QtQuick

/*!
    \qmltype ZStatusDot
    \inqmlmodule ZmdUI
    \brief 状态点：极小面积的切角方块，用来放点缀色（青 / 品红 / 亮黄）。

    设计规范里的青色、品红只允许出现在这个尺寸的块上。

    \qml
    ZStatusDot { tone: Theme.Tone.Cyan }
    ZStatusDot { tone: Theme.Tone.Danger; blink: true }
    \endqml
*/
Item {
    id: root

    /*! 色调，取 Theme.Tone.*（Cyan / Magenta / Accent / Ink / Danger） */
    property int tone: Theme.Tone.Accent
    /*! 边长 */
    property int size: 10
    /*! 是否呼吸闪烁（用于"连接中 / 异常"状态） */
    property bool blink: false

    implicitWidth: size
    implicitHeight: size

    ZCutRect {
        anchors.fill: parent
        color: {
            switch (root.tone) {
            case Theme.Tone.Cyan:    return Theme.cyan
            case Theme.Tone.Magenta: return Theme.magenta
            case Theme.Tone.Ink:     return Theme.ink
            case Theme.Tone.Danger:  return Theme.danger
            case Theme.Tone.Muted:   return Theme.lineSoft
            default:                 return Theme.accent
            }
        }
        strokeColor: Theme.ink
        strokeWidth: 1
        cutTopRight: root.size / 2
        cutBottomLeft: root.size / 2
    }

    SequentialAnimation on opacity {
        running: root.blink
        loops: Animation.Infinite
        NumberAnimation { to: 0.25; duration: 520; easing.type: Easing.InOutQuad }
        NumberAnimation { to: 1.0;  duration: 520; easing.type: Easing.InOutQuad }
    }
}
