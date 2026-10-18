import QtQuick

/*!
    \qmltype ZSectionTitle
    \inqmlmodule ZmdUI
    \brief 区块大标题：编号 + 「超粗中文标题」+ 黑底白字斜切副标题。

    这是设计语言里的"视觉锤"。标题用 Font.Black 再叠 1px 同色描边，
    在只有 Regular/Bold 两档字重的中文字体上也能做出 Heavy 的压迫感。

    \qml
    ZSectionTitle {
        index: "01"
        eyebrow: "MISSION-DEPENDENT PAYLOAD / SYSTEM INTERFACE"
        title: "组件总览"
        subtitle: "INTERFACE GALLERY"
    }
    \endqml
*/
Item {
    id: root

    /*! 编号，如 "01" */
    property string index: "01"
    /*! 标题上方的等宽小字（会加一个 + 前缀） */
    property string eyebrow: ""
    /*! 主标题，自动加「」 */
    property string title: ""
    /*! 副标题（黑底白字块），空则隐藏 */
    property string subtitle: ""
    /*! 主标题左侧的高亮底色块是否显示 */
    property bool highlight: true
    /*! 副标题块的倾斜量 */
    property real skewAmount: 12

    implicitWidth: col.implicitWidth
    implicitHeight: col.implicitHeight

    Column {
        id: col
        spacing: Theme.spacingSm

        // ── eyebrow：+ MISSION-DEPENDENT PAYLOAD ──
        Row {
            visible: root.eyebrow.length > 0
            spacing: Theme.spacingSm

            // 小十字星
            Item {
                anchors.verticalCenter: parent.verticalCenter
                width: 10
                height: 10
                Rectangle { anchors.centerIn: parent; width: 10; height: 1.5; color: Theme.ink }
                Rectangle { anchors.centerIn: parent; width: 1.5; height: 10; color: Theme.ink }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.eyebrow
                font: Theme.fontMonoSm
                color: Theme.ink
                opacity: 0.75
            }
        }

        // ── 主标题行 ──
        Item {
            width: titleRow.implicitWidth
            height: titleRow.implicitHeight

            // 高亮底色：盖住标题左侧 1/3，像荧光笔划过
            ZCutRect {
                visible: root.highlight
                anchors.left: parent.left
                anchors.leftMargin: -4
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 8
                width: Math.round(titleRow.textWidth * 0.36)
                height: Math.round(titleRow.height * 0.72)
                // 用 accentSoft 而不是 accent：亮色下它同样是亮黄（黑字压上去没问题），
                // 暗色下它是深黄褐，浅灰标题压上去才够对比；直接用压暗后的 accent
                // 会让"浅灰大字 + 中黄底"糊成一片
                color: Theme.accentSoft
                cutTopRight: 10
            }

            Row {
                id: titleRow
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                spacing: Theme.spacingSm

                property real textWidth: indexText.implicitWidth + titleText.implicitWidth
                                            + Theme.spacingLg

                // 编号：黑底白字小方块
                ZCutRect {
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 6
                    width: indexText.implicitWidth + Theme.spacingMd
                    height: 20
                    color: Theme.inkBlock
                    cutTopRight: 6

                    Text {
                        id: indexText
                        anchors.centerIn: parent
                        text: root.index
                        font: Theme.fontMonoMd
                        color: Theme.textOnSub
                    }
                }

                Text {
                    id: titleText
                    anchors.bottom: parent.bottom
                    text: root.title.length > 0 ? "「" + root.title + "」" : ""
                    font: Theme.fontTitle
                    color: Theme.ink
                }
            }
        }

        // ── 副标题：黑底白字平行四边形 ──
        ZSkewRect {
            width: subtitleText.implicitWidth + Theme.spacingXl
            height: 36
            visible: root.subtitle.length > 0
            color: Theme.subBg
            skew: root.skewAmount

            Row {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: -root.skewAmount / 2
                spacing: Theme.spacingSm

                // 左侧冒号：用两个方块拼出硬朗的“:”。
                // 用淡青而不是纯白——5px 的小方块上纯白边界发虚、和副标题文字抢视觉。
                Column {
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Rectangle { width: 5; height: 5; color: Theme.cyanSoft }
                    Rectangle { width: 5; height: 5; color: Theme.cyanSoft }
                }

                Text {
                    id: subtitleText
                    text: root.subtitle
                    font: Theme.fontSubtitle
                    color: Theme.textOnSub
                }
            }
        }
    }
}
