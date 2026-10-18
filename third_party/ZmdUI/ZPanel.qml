import QtQuick

/*!
    \qmltype ZPanel
    \inqmlmodule ZmdUI
    \brief 面板 / 卡片：斜切角 + 角落斜线纹理 + 底部亮黄装饰条。

    整套设计语言里最主要的容器。默认会把子项按列排布（内容区是个 Column），
    所以直接往里塞 Row / Column / 控件即可。

    \qml
    ZPanel {
        width: 420
        title: "运行概况"
        tag: "SYS-01"
        footerText: "ARKNIGHTS:ENDFIELD  /  BOT CONSOLE"
        Row { spacing: 8; ... }
    }
    \endqml

    \note 面板高度是按内容算出来的（implicitHeight），直接给定 width 即可自动撑高。
*/
Item {
    id: root

    /*! 标题，Font.Black + 自动加「」 */
    property string title: ""
    /*! 标题下方的黑底白字副标题 */
    property string subtitle: ""
    /*! 右上角等宽编号 / 坐标 */
    property string tag: ""
    /*! 副标题上方的一行等宽小字说明 */
    property string eyebrow: ""
    /*! 底部装饰条文字（等宽） */
    property string footerText: ""
    /*! 底部条色调：Theme.Tone.Accent（亮黄底黑字）或 Theme.Tone.Ink（深灰底白字） */
    property int footerTone: Theme.Tone.Accent
    /*! 是否绘制右上角斜线纹理 */
    property bool hatch: true
    /*! 斜线区宽度占比（0~1） */
    property real hatchRatio: 0.34
    /*! 斜线区高度占比（0~1） */
    property real hatchRatioV: 0.5
    /*! 主体底色：默认卡片白，可传 { Theme.surfaceAlt } 做次级卡片 */
    property color surfaceColor: Theme.surface
    /*! 是否使用大斜切角（小卡片可关掉） */
    property bool cut: true
    /*! 是否在标题左侧显示一条亮黄竖条 */
    property bool accentEdge: true

    default property alias content: contentColumn.data
    /*! 标题行右侧的自定义槽位（放按钮 / 状态点） */
    property alias headerRight: headerRightSlot.data

    implicitWidth: 320
    implicitHeight: body.implicitHeight + Theme.spacingLg
                    + (accentBar.visible ? accentBar.height + Theme.spacingSm : 0)

    // ── 底板 ──
    ZCutRect {
        anchors.fill: parent
        color: root.surfaceColor
        strokeColor: Theme.line
        strokeWidth: Theme.border
        cutTopRight: root.cut ? Theme.cutLg : Theme.cutSm
        cutBottomLeft: root.cut ? Theme.cutMd : Theme.cutSm
    }

    // ── 右上角斜线纹理区 ──
    Item {
        id: hatchArea
        visible: root.hatch
        anchors.top: parent.top
        anchors.right: parent.right
        width: Math.round(parent.width * root.hatchRatio)
        height: Math.round(parent.height * root.hatchRatioV)
        clip: true

        ZHatchPattern {
            anchors.fill: parent
            anchors.rightMargin: -8
            lineColor: Theme.hatch
            spacing: Theme.hatchStep
        }

        // 斜线区左下角的直角三角形标记。
        // 注意：这里不能用 ZCutRect 的切角来做三角——切角会被钳制到短边一半以内，
        // 10x10 的方块只会被削掉 5px 的角，看起来就是一个"多余的黑方块"。
        // ZSkewRect 的 skew 等于宽度时，两个顶点重合，正好得到干净的直角三角形。
        ZSkewRect {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            width: 12
            height: 12
            skew: 12
            color: Theme.inkBlock
        }
    }

    // ── 内容 ──
    Column {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.spacingLg
        anchors.bottomMargin: Theme.spacingSm
        spacing: Theme.spacingMd

        // 字段铭牌（等宽）
        Text {
            visible: root.eyebrow.length > 0
            text: root.eyebrow
            font: Theme.fontMonoSm
            color: Theme.muted
        }

        // 标题行
        Item {
            width: parent.width
            height: Math.max(titleRow.implicitHeight, 24)
            visible: root.title.length > 0 || root.tag.length > 0

            Row {
                id: titleRow
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingSm

                // 亮黄竖条
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: root.accentEdge
                    width: 6
                    height: 20
                    color: Theme.accent
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: root.title.length > 0 ? "「" + root.title + "」" : ""
                    font: Theme.fontHeading
                    color: Theme.ink
                    // 同色描边制造超粗观感
                }
            }

            // 右上角编号
            Text {
                anchors.right: parent.right
                anchors.top: parent.top
                text: root.tag
                font: Theme.fontMonoSm
                color: Theme.muted
            }

            // 右侧槽位
            Row {
                id: headerRightSlot
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                spacing: Theme.spacingSm
            }
        }

        // 副标题：黑底白字斜切块
        ZSkewRect {
            width: subtitleText.implicitWidth + Theme.spacingLg * 1.5
            height: 30
            visible: root.subtitle.length > 0
            color: Theme.subBg
            skew: 10

            Row {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: -3
                spacing: Theme.spacingSm
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 4
                    height: 14
                    color: Theme.accent
                }
                Text {
                    id: subtitleText
                    text: root.subtitle
                    font: Theme.fontSubtitle
                    color: Theme.textOnSub
                }
            }
        }

        // ── 用户内容 ──
        Column {
            id: contentColumn
            width: parent.width
            spacing: Theme.spacingMd
        }
    }

    // ── 底部装饰条 ──
    ZCutRect {
        id: accentBar
        visible: root.footerText.length > 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.dp(Theme.accentBarHeight)
        color: root.footerTone === Theme.Tone.Ink ? Theme.subBg : Theme.accent
        cutBottomLeft: root.cut ? Theme.cutMd : Theme.cutSm

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingLg
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingMd

            // 小三角形指示符
            Canvas {
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 8
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.fillStyle = root.footerTone === Theme.Tone.Ink
                                    ? Theme.accent : Theme.ink
                    ctx.beginPath()
                    ctx.moveTo(0, 0)
                    ctx.lineTo(8, 4)
                    ctx.lineTo(0, 8)
                    ctx.closePath()
                    ctx.fill()
                }
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.footerText
                font: Theme.fontMonoSm
                color: root.footerTone === Theme.Tone.Ink ? Theme.textOnSub : Theme.textOnAccent
            }
        }

        // 右侧坐标点阵
        Row {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingLg + Theme.cutMd
            anchors.verticalCenter: parent.verticalCenter
            spacing: 3
            Repeater {
                model: 4
                Rectangle {
                    width: 3
                    height: 10
                    color: root.footerTone === Theme.Tone.Ink ? Theme.textOnSub : Theme.textOnAccent
                    opacity: 0.35 + index * 0.18
                }
            }
        }
    }
}
