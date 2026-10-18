import QtQuick

/*!
    \qmltype ZTag
    \inqmlmodule ZmdUI
    \brief 标签 / 徽章：平行四边形外观，深灰底白字或亮黄底黑字。

    \l tone 取 \c Theme.Tone 之一：
    \list
    \li \c Ink     深灰底 + 白字（默认，信息类）
    \li \c Accent  亮黄底 + 黑字（强调 / 计数）
    \li \c Outline 透明底 + 黑框（次要）
    \li \c Muted   浅灰底 + 深灰字（禁用 / 占位）
    \li \c Cyan / \c Magenta  极小面积的点缀状态块
    \endlist

    \qml
    ZTag { text: "ONLINE"; tone: Theme.Tone.Accent }
    ZTag { text: "BOT 01"; tone: Theme.Tone.Ink; mono: false }
    \endqml
*/
Item {
    id: root

    /*! 文本 */
    property string text: ""
    /*! 色调，取 Theme.Tone.* */
    property int tone: Theme.Tone.Ink
    /*! 是否使用平行四边形外观（关闭则为斜切角矩形） */
    property bool skew: true
    /*! 倾斜量 */
    property real skewAmount: 8
    /*! 是否使用等宽字体 */
    property bool mono: true
    /*! 是否把文本转成大写 */
    property bool uppercase: true
    /*! 文本左侧的状态点颜色，透明则不画 */
    property color dotColor: "transparent"

    readonly property string _text: uppercase ? text.toUpperCase() : text

    implicitWidth: tm.advanceWidth + Theme.spacingMd * 2 + (skew ? skewAmount : 0)
                                + (dotColor.a > 0 ? 12 : 0)
    implicitHeight: 22

    TextMetrics {
        id: tm
        text: root._text
        font: root.mono ? Theme.fontMonoSm : Theme.fontLabel
    }

    // ── 底板 ──
    ZSkewRect {
        anchors.fill: parent
        visible: root.skew
        skew: root.skewAmount
        color: root._bg
        strokeColor: root._stroke
        strokeWidth: root.tone === Theme.Tone.Outline ? 2 : 0
    }

    ZCutRect {
        anchors.fill: parent
        visible: !root.skew
        color: root._bg
        strokeColor: root._stroke
        strokeWidth: root.tone === Theme.Tone.Outline ? 2 : 0
        cutTopRight: 6
        cutBottomLeft: 3
    }

    readonly property color _bg: {
        switch (tone) {
        case Theme.Tone.Ink:     return Theme.subBg
        case Theme.Tone.Accent:  return Theme.accent
        case Theme.Tone.Muted:   return Theme.surfaceAlt
        case Theme.Tone.Cyan:    return Theme.cyan
        case Theme.Tone.Magenta: return Theme.magenta
        case Theme.Tone.Danger:  return Theme.danger
        default:                 return "transparent"
        }
    }

    readonly property color _stroke: tone === Theme.Tone.Outline ? Theme.ink : "transparent"

    readonly property color _fg: {
        switch (tone) {
        case Theme.Tone.Ink:     return Theme.textOnSub
        case Theme.Tone.Accent:  return Theme.textOnAccent
        case Theme.Tone.Muted:   return Theme.muted
        case Theme.Tone.Cyan:    return "#00313A"
        case Theme.Tone.Magenta: return "#3A003A"
        case Theme.Tone.Danger:  return Theme.textOnSub
        default:                 return Theme.ink
        }
    }

    Row {
        anchors.centerIn: parent
        anchors.horizontalCenterOffset: root.skew ? -root.skewAmount / 2 : 0
        spacing: 5

        // 状态点：极小面积的点缀色出现位置
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            visible: root.dotColor.a > 0
            width: 6
            height: 6
            color: root.dotColor
        }

        Text {
            id: label
            text: root._text
            font: root.mono ? Theme.fontMonoSm : Theme.fontLabel
            color: root._fg
        }
    }
}
