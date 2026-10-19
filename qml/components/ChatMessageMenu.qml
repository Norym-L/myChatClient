import QtQuick
import QtQuick.Controls.Basic
import ZmdUI 1.0

/*!
    一条消息的右键菜单：复制 / 删除 / （将来的）添加到表情包。

    为什么自己画而不是用 QtQuick.Controls 的 Menu：
    默认样式是浅灰圆角，和这套"黑黄终端"设计语言完全不搭；
    这里复用 ZCutRect 之外最简单的 Rectangle 描边，和 ZDialog 的边框一致。

    \note 位置由调用方给定（x / y 是相对页面根节点的坐标），
          组件自己只负责把内容画出来，并往外发两个信号。
*/
Popup {
    id: root

    /*! 被右键的那条消息正文（复制时用） */
    property string messageText: ""

    signal copyRequested()
    signal deleteRequested()

    width: 184
    padding: 0
    modal: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Theme.surface
        border.width: Theme.border
        border.color: Theme.line
    }

    contentItem: Item {
        implicitWidth: root.width
        implicitHeight: menuColumn.implicitHeight + Theme.spacingSm * 2

        Column {
            id: menuColumn
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingSm

            MenuRow {
                label: "复制"
                onActivated: root.copyRequested()
            }

            MenuRow {
                label: "删除"
                danger: true
                onActivated: root.deleteRequested()
            }

            // 分隔线：把"已经能用的"和"将来才有的"分开
            Rectangle {
                width: parent.width
                height: 1
                color: Theme.lineSoft
                opacity: 0.7
            }

            MenuRow {
                label: "添加到表情包"
                enabledRow: false
                hint: "后续版本"
            }
        }
    }

    /*!
        一行菜单项。

        没做 l highlighted 之类的状态机：hover 用 HoverHandler 在手，
        禁用项只是降低不透明度 + 不响应点击 —— 这样"暂未开放"的能力
        仍然可见（用户知道以后会有），但不会假装能点。
    */
    component MenuRow: Rectangle {
        id: row

        property string label: ""
        property string hint: ""
        property bool danger: false
        property bool enabledRow: true

        signal activated()

        width: parent ? parent.width : 0
        height: 34
        color: (rowHover.hovered && row.enabledRow) ? Theme.surfaceHover : "transparent"
        opacity: row.enabledRow ? 1 : 0.38

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            text: row.label
            font: Theme.fontBody
            color: row.danger ? Theme.danger : Theme.ink
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            text: row.hint
            font: Theme.fontMonoSm
            color: Theme.muted
        }

        HoverHandler {
            id: rowHover
            cursorShape: row.enabledRow ? Qt.PointingHandCursor : Qt.ArrowCursor
        }

        TapHandler {
            enabled: row.enabledRow
            onTapped: row.activated()
        }
    }
}
