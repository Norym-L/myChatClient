import QtQuick
import QtQuick.Controls.Basic

/*!
    \qmltype ZIconButton
    \inqmlmodule ZmdUI
    \brief 方形图标按钮：斜切角方框 + 单个字符图形，自带工具提示。

    不依赖字体图标库，直接用等宽字符当图形（+ − × → ↗ ≡ ◎ ⌕），
    也可以用 \l source 挂一张图片。

    \qml
    ZIconButton { glyph: "+"; tooltip: "新建任务"; onClicked: ... }
    ZIconButton { glyph: "×"; tone: Theme.Tone.Danger; tooltip: "关闭" }
    \endqml
*/
Button {
    id: control

    /*! 图形字符 */
    property string glyph: "+"
    /*! 图形字符的字号 */
    property int glyphSize: 18
    /*! 色调，取 Theme.Tone.*（Ink / Accent / Outline / Danger） */
    property int tone: Theme.Tone.Outline
    /*! 提示文案，非空时悬停自动弹出 ZToolTip */
    property string tooltip: ""
    /*! 备用：图片图形，设置后覆盖 glyph */
    property url source

    hoverEnabled: true
    implicitWidth: Theme.dp(Theme.controlHeight)
    implicitHeight: Theme.dp(Theme.controlHeight)
    padding: 0

    readonly property bool _on: hovered || visualFocus

    background: ZCutRect {
        anchors.fill: parent
        color: {
            if (!control.enabled)
                return Theme.surfaceAlt
            if (control._on) {
                return control.tone === Theme.Tone.Outline ? Theme.accent
                     : control.tone === Theme.Tone.Danger ? Theme.danger
                     : Theme.ink
            }
            return control.tone === Theme.Tone.Accent ? Theme.accent
                 : control.tone === Theme.Tone.Danger ? Theme.subBg
                 : Theme.surface
        }
        // 用结构线而不是 ink：暗色下 ink 是浅灰，一圈白边在暗色界面里过于抢眼
        strokeColor: control.enabled ? Theme.line : Theme.lineSoft
        strokeWidth: Theme.border
        cutTopRight: Theme.cutSm
        cutBottomLeft: Theme.cutSm
    }

    contentItem: Item {
        implicitWidth: Theme.dp(Theme.controlHeight) - Theme.border * 2
        implicitHeight: Theme.dp(Theme.controlHeight) - Theme.border * 2

        Text {
            anchors.centerIn: parent
            visible: !control.source.toString()
            text: control.glyph
            // 等宽字符做图形，横向拉伸一点更像工业符号
            font.family: Theme.familyMono
            font.pixelSize: control.glyphSize
            font.bold: true
            color: {
                if (!control.enabled)
                    return Theme.muted
                if (control._on)
                    return control.tone === Theme.Tone.Outline ? Theme.ink : Theme.textOnSub
                return control.tone === Theme.Tone.Accent ? Theme.textOnAccent
                     : control.tone === Theme.Tone.Danger ? Theme.textOnSub
                     : Theme.ink
            }
        }

        Image {
            anchors.centerIn: parent
            visible: !!control.source.toString()
            source: control.source
            sourceSize.width: 20 * Screen.devicePixelRatio
            sourceSize.height: 20 * Screen.devicePixelRatio
        }
    }

    ZToolTip {
        text: control.tooltip
        visible: control.tooltip.length > 0 && control.hovered
    }
}
