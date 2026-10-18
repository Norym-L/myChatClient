import QtQuick
import QtQuick.Controls.Basic

/*!
    \qmltype ZListView
    \inqmlmodule ZmdUI
    \brief 列表视图：斜切角外框 + 自带 ZScrollBar + 默认 ZListItem 委托。

    \l model 可以是 JS 数组或 ListModel。不传 delegate 时用 ZListItem，
    此时数组元素为字符串或含 label/description 字段的对象。

    \qml
    ZListView {
        width: 460; height: 260
        model: ["扫码登录", "余额同步", "模型检测"]
        onActivated: (i) => console.log(i)
    }
    \endqml
*/
Item {
    id: root

    /*! 数据模型 */
    property alias model: view.model
    /*! 委托，缺省为 ZListItem */
    property alias delegate: view.delegate
    /*! 当前项索引 */
    property alias currentIndex: view.currentIndex
    /*! 条目数 */
    property alias count: view.count
    /*! 行距 */
    property alias spacing: view.spacing
    /*! 是否显示外框 */
    property bool frame: true
    /*! 外框标题（等宽小字，显示在列表上方） */
    property string headerText: ""

    /*! 点击某一行（委托用 ZListItem 时自动转发） */
    signal activated(int index)

    implicitWidth: 320
    implicitHeight: 240

    // ── 外框 ──
    ZCutRect {
        anchors.fill: parent
        visible: root.frame
        color: Theme.surface
        strokeColor: Theme.line
        strokeWidth: Theme.border
        cutBottomLeft: Theme.cutMd
        cutTopRight: Theme.cutSm
    }

    // ── 顶部标题带 ──
    ZCutRect {
        id: headerBand
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.border
        height: root.headerText.length > 0 ? 26 : 0
        visible: root.headerText.length > 0
        color: Theme.subBg

        Row {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingMd
            anchors.verticalCenter: parent.verticalCenter
            spacing: Theme.spacingSm
            Rectangle {
                anchors.verticalCenter: parent.verticalCenter
                width: 6
                height: 10
                color: Theme.accent
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.headerText
                font: Theme.fontMonoSm
                color: Theme.textOnSub
            }
        }
    }

    ListView {
        id: view
        anchors.fill: parent
        anchors.margins: Theme.border + (headerBand.height > 0 ? headerBand.height : 0)
        anchors.topMargin: Theme.border + headerBand.height + (headerBand.height > 0 ? 2 : 0)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        spacing: 0
        highlightFollowsCurrentItem: false

        ScrollBar.vertical: ZScrollBar {
            policy: ScrollBar.AsNeeded
        }

        // 缺省委托：兼容字符串数组与 {label, description} 对象数组
        delegate: ZListItem {
            width: view.width
            idx: (index + 1 < 10 ? "0" : "") + (index + 1)
            label: (typeof modelData === "object" && modelData !== null)
                   ? (modelData.label !== undefined ? modelData.label : "")
                   : String(modelData === undefined ? "" : modelData)
            description: (typeof modelData === "object" && modelData !== null
                          && modelData.description !== undefined) ? modelData.description : ""
            trailing: (typeof modelData === "object" && modelData !== null
                       && modelData.value !== undefined) ? String(modelData.value) : ""
            selected: ListView.isCurrentItem
            onClicked: {
                view.currentIndex = index
                root.activated(index)
            }
            onDoubleClicked: root.activated(index)
        }
    }
}
