import QtQuick
import QtQuick.Controls.Basic

/*!
    \qmltype ZSideBar
    \inqmlmodule ZmdUI
    \brief 侧边导航栏：恒定黑底白字，当前项亮黄底黑字 + 亮黄左边缘装饰。

    \l model 是对象数组，支持字段：
    \list
    \li \c header  可选，在该项上方插一条等宽分组标题
    \li \c label   行文本
    \li \c badge   右侧等宽小字（计数 / 编号）
    \endlist

    \qml
    ZSideBar {
        height: parent.height
        model: [
            { header: "01 / 工作台", label: "运行概况", badge: "→" },
            { label: "扫码登录" },
            { header: "02 / 系统管理", label: "日志中心", badge: "12" }
        ]
        onActivated: (i) => page.currentIndex = i
    }
    \endqml
*/
Item {
    id: root

    /*! 导航数据 */
    property var model: []
    /*! 当前选中项 */
    property int currentIndex: 0
    /*! 品牌主标题 */
    property string brand: "ZMDUI"
    /*! 品牌副标题（亮黄等宽小字） */
    property string brandSub: "INDUSTRIAL INTERFACE"
    /*! 品牌区上方的编号 */
    property string brandCode: "SYS / 00"
    /*! 底部版本信息（等宽） */
    property string version: ""
    /*!
        导航溢出时是否显示滚动条。设 false 可完全关掉（内容仍能用滚轮 / 拖拽滚动）。
    */
    property bool navScrollBar: true
    /*!
        滚动条占位宽度。**始终预留**：Qt 给 Flickable 挂的滚动条是浮在内容之上的，
        不预留的话它会盖住行右侧的编号 / 徽标（用户反馈过这个）。
    */
    readonly property int navScrollBarReserve: navScrollBar ? 14 : 0

    /*! 点击某项 */
    signal activated(int index)

    default property alias footerItems: footerCol.data

    implicitWidth: 236
    implicitHeight: 480

    // 侧边栏在主色板上属于"深灰 / 纯黑"那一档，不跟随亮暗主题反转；
    // 只用不同黑度区分：亮色主题更黑，暗色主题略浅以便与背景分层。
    // 侧边栏在两个主题里都是"深色带"，但暗色下必须比页面底色（#181818）更深，
    // 否则它会和背景糊成一片（之前 #161616 几乎等于底色，悬停块看起来像污渍）。
    readonly property color panelColor: Theme.isDark ? "#0C0C0C" : "#101010"
    readonly property color textColor:  "#FFFFFF"
    readonly property color textMuted:  "#8C8C8C"

    // ── 底板 ──
    Rectangle {
        anchors.fill: parent
        color: root.panelColor
    }

    // 右边缘：上段亮黄 + 下段深灰细线
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        width: 3
        height: parent.height * 0.32
        color: Theme.accent
    }
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: parent.height * 0.32
        width: 1
        height: parent.height * 0.68
        color: "#FFFFFF"
        opacity: 0.22
    }

    // ══ 品牌区 ══
    Item {
        id: brandBlock
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 104

        // 右上角黄色斜块装饰
        ZCutRect {
            anchors.right: parent.right
            anchors.top: parent.top
            width: 54
            height: 26
            color: Theme.accent
            cutBottomLeft: 22
        }

        Column {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingLg
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingMd
            spacing: 2

            Text {
                text: root.brandCode
                font: Theme.fontMonoSm
                color: Theme.accent
            }

            Text {
                text: root.brand
                // 品牌用超粗无衬线，叠同色描边做加粗
                font: Qt.font({ family: Theme.family, pixelSize: 34,
                                weight: Font.Black, letterSpacing: 1 })
                color: root.textColor
            }

            Text {
                text: root.brandSub
                font: Theme.fontMonoSm
                color: Theme.accent
            }
        }
    }

    // 品牌区下的粗黄分隔条
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: brandBlock.bottom
        height: 4
        color: Theme.accent
    }

    // ══ 底部区（版本 + 自定义页脚）══
    Item {
        id: footerArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Math.max(footerCol.implicitHeight + versionText.implicitHeight
                         + Theme.spacingSm * 2 + Theme.spacingMd * 2, 96)

        Text {
            id: versionText
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingLg
            anchors.bottom: parent.bottom
            anchors.bottomMargin: Theme.spacingMd
            text: root.version
            font: Theme.fontMonoSm
            color: root.textMuted
            visible: root.version.length > 0
        }

        Column {
            id: footerCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: versionText.visible ? versionText.top : parent.bottom
            anchors.bottomMargin: Theme.spacingMd
            spacing: Theme.spacingSm
        }
    }

    // ══ 导航列表 ══
    Flickable {
        id: navFlick
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: brandBlock.bottom
        anchors.topMargin: 4
        anchors.bottom: footerArea.top
        contentWidth: width
        contentHeight: navCol.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height
        ScrollBar.vertical: ZScrollBar {
            policy: root.navScrollBar ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
        }

        Column {
            id: navCol
            width: navFlick.width

            Repeater {
                model: root.model
                delegate: Column {
                    id: entryCol
                    width: navCol.width

                    readonly property var entry: modelData
                    readonly property bool isMap: (typeof entry === "object" && entry !== null)
                    readonly property string entryLabel: isMap
                            ? (entry.label !== undefined ? entry.label : "") : String(entry)
                    readonly property string entryBadge: (isMap && entry.badge !== undefined)
                            ? String(entry.badge) : ""
                    readonly property string entryHeader: (isMap && entry.header !== undefined)
                            ? String(entry.header) : ""

                    // ── 分组标题 ──
                    Item {
                        width: entryCol.width
                        height: entryCol.entryHeader.length > 0 ? 40 : 0
                        visible: entryCol.entryHeader.length > 0

                        Row {
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacingLg
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spacingSm

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: 12
                                height: 2
                                color: Theme.accent
                                opacity: 0.7
                            }
                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: entryCol.entryHeader
                                font: Theme.fontMonoSm
                                color: root.textMuted
                            }
                        }
                    }

                    // ── 导航行 ──
                    Item {
                        id: rowItem
                        width: entryCol.width
                        height: 46

                        readonly property bool current: index === root.currentIndex
                        readonly property bool isHovered: rowHover.hovered

                        HoverHandler { id: rowHover }

                        // 选中：亮黄底
                        ZCutRect {
                            anchors.fill: parent
                            anchors.margins: 2
                            visible: rowItem.current
                            color: Theme.accent
                            cutTopRight: 8
                            cutBottomLeft: 8
                        }

                        // 悬停：深灰底
                        ZCutRect {
                            anchors.fill: parent
                            anchors.margins: 2
                            visible: rowItem.isHovered && !rowItem.current
                            color: Theme.isDark ? "#1E1E1E" : "#232323"
                            cutTopRight: 8
                            cutBottomLeft: 8
                        }

                        // 左边缘装饰条
                        ZCutRect {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            anchors.topMargin: 6
                            anchors.bottomMargin: 6
                            width: 5
                            color: rowItem.current ? "#111111"
                                 : (rowItem.isHovered ? Theme.accent : "transparent")
                            cutTopRight: 4
                            cutBottomLeft: 4
                        }

                        Row {
                            id: rowContent
                            anchors.left: parent.left
                            anchors.leftMargin: Theme.spacingLg + 4
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.spacingMd + root.navScrollBarReserve
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Theme.spacingSm

                            Text {
                                anchors.verticalCenter: parent.verticalCenter
                                text: entryCol.entryLabel
                                font: Theme.fontStrong
                                color: rowItem.current ? Theme.textOnAccent : root.textColor
                                elide: Text.ElideRight
                                width: Math.max(0, rowContent.width - badgeText.width
                                                   - (badgeText.text.length > 0 ? 18 : 0))
                            }

                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                visible: badgeText.text.length > 0
                                width: visible ? 6 : 0
                                height: 6
                                color: rowItem.current ? Theme.textOnAccent : Theme.accent
                            }

                            Text {
                                id: badgeText
                                anchors.verticalCenter: parent.verticalCenter
                                text: entryCol.entryBadge
                                font: Theme.fontMonoSm
                                color: rowItem.current ? Theme.textOnAccent : root.textMuted
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                root.currentIndex = index
                                root.activated(index)
                            }
                        }
                    }
                }
            }
        }
    }
}
