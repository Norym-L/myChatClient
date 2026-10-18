import QtQuick
import QtQuick.Controls.Basic

/*!
    \qmltype ZDialog
    \inqmlmodule ZmdUI
    \brief 对话框：顶部黑底黄字标题带，中央大面积斜线纹理，硬边斜切外框。

    \qml
    ZDialog {
        id: warn
        title: "覆盖本地配置"
        code: "SYS-04"
        subtitle: "此操作不可撤销"
        confirmText: "确认覆盖"
        Column { Text { text: "……" } }
        onConfirmed: doIt()
    }
    \endqml
*/
Dialog {
    id: control

    /*! 标题（黑底黄字，等宽大写）。直接复用 Dialog 自带的 title 属性 */
    title: "系统提示"
    /*! 右上角编号 */
    property string code: "SYS-00"
    /*! 正文上方的副标题（黑底白字小块） */
    property string subtitle: ""
    /*! 底部主按钮文案 */
    property string confirmText: "确认执行"
    /*! 底部次按钮文案 */
    property string cancelText: "取消"
    /*! 是否显示底部按钮区 */
    property bool showFooter: true
    /*! 主按钮形态 */
    property int confirmVariant: Theme.Variant.Primary
    /*! 是否允许 Esc 关闭 */
    property bool closable: true

    default property alias dialogContent: innerCol.data
    property alias footerItems: footerSlot.data


    /*! 主按钮点击后触发（在关闭之前） */
    signal confirmed()

    modal: true
    focus: true
    closePolicy: control.closable ? Popup.CloseOnEscape : Popup.NoAutoClose
    anchors.centerIn: Overlay.overlay
    width: 540
    padding: 0

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: Theme.durSlow }
        NumberAnimation { property: "scale"; from: 0.94; to: 1; duration: Theme.durSlow
                          easing.type: Theme.easingEnter }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: Theme.durBase }
    }

    // ── 遮罩：平涂，不加渐变 ──
    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.55)
    }

    // ── 底板 ──
    background: Item {
        ZCutRect {
            anchors.fill: parent
            color: Theme.surface
            strokeColor: Theme.ink
            strokeWidth: Theme.borderThick
            cutTopRight: 28
            cutBottomLeft: 14
        }

        // 中央大面积斜线纹理：对话框最显著的质感
        Item {
            anchors.centerIn: parent
            width: parent.width * 0.9
            height: parent.height * 0.5
            clip: true
            opacity: 0.55
            ZHatchPattern {
                anchors.fill: parent
                lineColor: Theme.hatchStrong
                spacing: 11
                lineWidth: 1.2
            }
        }

        // 左下角一角黄
        ZCutRect {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            anchors.margins: Theme.borderThick + 2
            width: 26
            height: 8
            color: Theme.accent
            cutBottomLeft: 8
        }
    }

    // ── 标题带 ──
    header: Item {
        implicitHeight: 46

        ZCutRect {
            anchors.fill: parent
            color: Theme.inkBlock
            cutTopRight: 28
        }

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingMd

            // 黄黑方格 logo
            Row {
                anchors.verticalCenter: parent.verticalCenter
                spacing: 2
                Repeater {
                    model: 3
                    ZCutRect {
                        width: 8
                        height: 16
                        color: index === 1 ? Theme.accent : Theme.textOnSub
                        cutTopRight: 3
                    }
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.title.toUpperCase()
                font: Qt.font({ family: Theme.family, pixelSize: 16,
                                weight: Font.DemiBold, letterSpacing: 2 })
                color: Theme.accent
            }
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingXl + 6
            anchors.verticalCenter: parent.verticalCenter
            text: control.code
            font: Theme.fontMonoSm
            color: Theme.textOnSub
            opacity: 0.7
        }

        ZIconButton {
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            visible: control.closable
            width: 26
            height: 26
            implicitWidth: 26
            implicitHeight: 26
            glyphSize: 14
            glyph: "×"
            tone: Theme.Tone.Accent
            tooltip: "关闭"
            onClicked: control.reject()
        }
    }

    // ── 内容 ──
    // 用「Item + 内层 Column」而不是直接给 Column 加 padding：
    // Column 的 padding 不会缩减子项的 parent.width，用户写 width: parent.width 会溢出被裁。
    contentItem: Item {
        implicitHeight: innerCol.implicitHeight + Theme.spacingLg * 2

        Column {
            id: innerCol
            x: Theme.spacingLg
            y: Theme.spacingLg
            width: parent.width - Theme.spacingLg * 2
            spacing: Theme.spacingMd

            // 副标题小块
            ZSkewRect {
                width: subtitleItem.implicitWidth + Theme.spacingLg * 1.4
                height: 28
                visible: control.subtitle.length > 0
                color: Theme.subBg
                skew: 9

                Text {
                    id: subtitleItem
                    anchors.centerIn: parent
                    anchors.horizontalCenterOffset: -4
                    text: control.subtitle
                    font: Theme.fontSubtitle
                    color: Theme.textOnSub
                }
            }
        }
    }

    // ── 底部按钮区 ──
    footer: Item {
        implicitHeight: control.showFooter ? 62 : 0
        visible: control.showFooter

        // 顶部分隔线
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Theme.spacingLg
            anchors.rightMargin: Theme.spacingXl
            height: 2
            color: Theme.line
        }

        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingLg
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingMd
            spacing: Theme.spacingSm

            Row {
                id: footerSlot
                spacing: Theme.spacingSm
            }

            ZButton {
                text: control.cancelText
                variant: Theme.Variant.Secondary
                onClicked: control.reject()
            }

            ZButton {
                text: control.confirmText
                variant: control.confirmVariant
                onClicked: {
                    control.confirmed()
                    control.accept()
                }
            }
        }
    }
}
