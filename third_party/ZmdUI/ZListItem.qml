import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Shapes

/*!
    \qmltype ZListItem
    \inqmlmodule ZmdUI
    \brief 列表项：编号 + 主标签 + 说明 + 右侧数据，选中时亮黄底 + 黑色斜切高亮块。

    \qml
    ListView {
        delegate: ZListItem {
            index: model.index
            label: model.label
            description: model.role
            trailing: model.value
        }
    }
    \endqml
*/
ItemDelegate {
    id: control

    /*! 左侧等宽编号。命名成 idx 是为了不遮蔽 ListView 的 index 角色 */
    property string idx: ""
    /*! 主标签 */
    property string label: ""
    /*! 说明文字（等宽小字） */
    property string description: ""
    /*! 右侧数据 / 编号 */
    property string trailing: ""
    /*! 选中态。可直接绑定 ListView.isCurrentItem */
    property bool selected: false
    /*! 是否显示右侧箭头指示符 */
    property bool arrow: true
    /*! 左侧强调条宽度（0 = 不显示） */
    property int accentWidth: 5

    readonly property bool _on: selected || highlighted

    hoverEnabled: true
    implicitHeight: Theme.dp(52)
    leftPadding: Theme.spacingMd + 6
    rightPadding: Theme.spacingMd
    topPadding: Theme.spacingSm
    bottomPadding: Theme.spacingSm

    background: Item {
        // ── 选中：亮黄底 ──
        ZCutRect {
            anchors.fill: parent
            anchors.margins: 1
            visible: control._on
            color: Theme.accent
            cutTopRight: Theme.cutSm
            cutBottomLeft: Theme.cutSm
        }

        // ── 悬停：浅灰底 + 亮黄左条 ──
        ZCutRect {
            anchors.fill: parent
            anchors.margins: 1
            visible: control.hovered && !control._on
            color: Theme.surfaceHover
            cutTopRight: Theme.cutSm
        }

        // ── 左侧强调条（当前项亮黄 / 悬停深灰）──
        ZCutRect {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 4
            anchors.bottomMargin: 4
            width: visible ? control.accentWidth : 0
            visible: control.accentWidth > 0
            color: control._on ? Theme.ink : (control.hovered ? Theme.accent : "transparent")
            cutTopRight: 4
            cutBottomLeft: 4
        }

        // ── 底部分隔线 ──
        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: control.leftPadding
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.lineSoft
            opacity: 0.55
            visible: !control._on
        }
    }

    contentItem: Row {
        spacing: Theme.spacingMd

        // 编号
        Text {
            anchors.verticalCenter: parent.verticalCenter
            width: 22
            visible: control.idx.length > 0
            text: control.idx
            font: Theme.fontMonoMd
            color: control._on ? Theme.ink : Theme.muted
        }

        Column {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 22 - arrowItem.width - slash.width
                   - trailingText.width - Theme.spacingMd * 4
            spacing: 1

            Text {
                text: control.label
                font: Theme.fontStrong
                // 选中行底色是强调黄，文字必须用"强调色上的文字色"，不能用 ink
                color: control._on ? Theme.textOnAccent : Theme.inkSoft
                elide: Text.ElideRight
                width: parent.width
            }

            Text {
                visible: control.description.length > 0
                text: control.description
                font: Theme.fontMonoSm
                color: control._on ? Theme.textOnAccent : Theme.muted
                opacity: control._on ? 0.75 : 1
                elide: Text.ElideRight
                width: parent.width
            }
        }

        // 选中态的黑色斜切高亮块：放在 Column 与右侧数据之间。
        // 放进内容布局而不是背景层，是为了让它参与排版——否则会盖住右侧数据文字。
        ZSkewRect {
            id: slash
            anchors.verticalCenter: parent.verticalCenter
            width: control._on ? 34 : 0
            height: 9
            skew: 9
            color: Theme.textOnAccent
            visible: control._on
        }

        // 右侧数据
        Text {
            id: trailingText
            anchors.verticalCenter: parent.verticalCenter
            visible: control.trailing.length > 0
            text: control.trailing
            font: Theme.fontMonoMd
            color: control._on ? Theme.textOnAccent : Theme.inkSoft
        }

        // 箭头指示符：Shape 三角，声明式绘制，状态变化自动重绘
        Item {
            id: arrowItem
            anchors.verticalCenter: parent.verticalCenter
            width: control.arrow ? 16 : 0
            height: 16
            visible: control.arrow

            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 8
                height: 2
                color: control._on ? Theme.ink : Theme.muted
            }

            Shape {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 6
                height: 10
                antialiasing: true
                preferredRendererType: Shape.CurveRenderer
                ShapePath {
                    strokeColor: "transparent"
                    strokeWidth: 0
                    fillColor: control._on ? Theme.ink : Theme.muted
                    PathPolyline {
                        path: [Qt.point(0, 0), Qt.point(6, 5), Qt.point(0, 10), Qt.point(0, 0)]
                    }
                }
            }
        }
    }
}
