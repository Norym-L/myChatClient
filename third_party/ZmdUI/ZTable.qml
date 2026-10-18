import QtQuick

/*!
    \qmltype ZTable
    \inqmlmodule ZmdUI
    \brief 数据表格：黑底表头 + 工程网格线 + 选中行亮黄高亮。

    \l columns 是列定义数组：\c {title, key, width, align}；
    \l rows 是数据数组（每项是对象）。\c width 省略的列会均分剩余宽度。

    \qml
    ZTable {
        width: 620
        columns: [
            { title: "模块", key: "name", width: 150 },
            { title: "状态", key: "state", width: 90 },
            { title: "延迟", key: "latency", align: "right" }
        ]
        rows: [
            { name: "GateServer", state: "在线", latency: "12 ms" },
            { name: "ChatServer", state: "在线", latency: "18 ms" }
        ]
    }
    \endqml
*/
Item {
    id: root

    /*! 列定义 */
    property var columns: []
    /*! 数据行 */
    property var rows: []
    /*! 当前选中行（-1 = 无） */
    property int currentIndex: -1
    /*! 表格上方等宽说明 */
    property string caption: ""
    /*! 底部装饰条文字 */
    property string footerText: ""
    /*! 行高 */
    property int rowHeight: 38
    /*!
        行点击是否抢占鼠标（对应 MouseArea.preventStealing）。

        \note 表格放在 Flickable 里时，按下之后指针只要移动超过系统拖拽阈值
        （Windows 默认 10px），滚动容器就会把这次按下**当成滚动手势抢走**，
        MouseArea 永远收不到 clicked —— 表现就是"点几次才中一次"。
        打开后按下即由行接管，点击必中；代价是在表格上按住拖拽不再滚动页面
        （滚轮和滚动条不受影响）。数据表格以点选为主，所以默认打开。
    */
    property bool rowClickRobust: true

    signal rowClicked(int index)

    readonly property int headerHeight: caption.length > 0 ? 50 : 32
    implicitWidth: 560
    implicitHeight: headerHeight + rows.length * rowHeight
                    + (footerText.length > 0 ? Theme.accentBarHeight : 0) + Theme.border * 2

    // ── 列宽缓存：width / columns 变化时自动重算 ──
    readonly property var _widths: {
        var out = []
        var fixed = 0
        var flexCount = 0
        var n = columns ? columns.length : 0
        for (var i = 0; i < n; i++) {
            var w = columns[i].width !== undefined ? columns[i].width : 0
            if (w > 0)
                fixed += w
            else
                flexCount++
        }
        var inner = width - Theme.border * 2 - 1
        var flexW = flexCount > 0 ? Math.max(60, (inner - fixed) / flexCount) : 0
        for (var k = 0; k < n; k++) {
            var w2 = columns[k].width !== undefined ? columns[k].width : 0
            out.push(w2 > 0 ? w2 : flexW)
        }
        return out
    }

    function xAt(i) {
        var x = 0
        for (var k = 0; k < i; k++)
            x += _widths[k] || 0
        return x
    }

    function cellText(row, col) {
        if (row === undefined || row === null || col === undefined)
            return ""
        var v = row[col.key]
        return v === undefined ? "" : String(v)
    }

    // ── 外框 ──
    ZCutRect {
        anchors.fill: parent
        color: Theme.surface
        strokeColor: Theme.line
        strokeWidth: Theme.border
        cutTopRight: Theme.cutMd
        cutBottomLeft: Theme.cutSm
    }

    // ══ 表头 ══
    Item {
        id: headerArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: Theme.border
        height: root.headerHeight

        // caption：等宽说明 + 小十字
        Row {
            id: captionRow
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingMd
            anchors.top: parent.top
            anchors.topMargin: Theme.spacingSm
            spacing: Theme.spacingSm
            visible: root.caption.length > 0

            Item {
                anchors.verticalCenter: parent.verticalCenter
                width: 9
                height: 9
                Rectangle { anchors.centerIn: parent; width: 9; height: 1.5; color: Theme.ink }
                Rectangle { anchors.centerIn: parent; width: 1.5; height: 9; color: Theme.ink }
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.caption
                font: Theme.fontMonoSm
                color: Theme.muted
            }
        }

        // 黑底表头
        ZCutRect {
            id: headerBand
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 28
            color: Theme.inkBlock

            Row {
                anchors.fill: parent
                Repeater {
                    model: root.columns
                    Item {
                        width: root._widths[index] || 0
                        height: headerBand.height

                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Theme.spacingMd
                            anchors.rightMargin: Theme.spacingMd
                            anchors.verticalCenter: parent.verticalCenter
                            text: (modelData.title !== undefined ? modelData.title : "").toUpperCase()
                            font: Theme.fontMonoSm
                            color: Theme.textOnSub
                            elide: Text.ElideRight
                            horizontalAlignment: modelData.align === "right" ? Text.AlignRight
                                             : modelData.align === "center" ? Text.AlignHCenter
                                             : Text.AlignLeft
                        }

                        // 列分隔刻度
                        Rectangle {
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: 1
                            height: 12
                            color: Theme.textOnSub
                            opacity: 0.35
                        }
                    }
                }
            }
        }
    }

    // ══ 数据行 ══
    Repeater {
        id: rowRepeater
        model: root.rows

        delegate: Item {
            id: rowDelegate
            width: root.width - Theme.border * 2
            height: root.rowHeight
            x: Theme.border
            y: Theme.border + root.headerHeight + index * root.rowHeight

            // Repeater 的 modelData 是上下文属性，必须显式捕获才能在子项里引用
            readonly property var rowData: modelData
            readonly property bool isCurrent: index === root.currentIndex
            readonly property bool isHovered: rowHover2.hovered

            HoverHandler { id: rowHover2 }

            // 交替底色
            Rectangle {
                anchors.fill: parent
                color: index % 2 === 0 ? Theme.surface : Theme.surfaceAlt
                opacity: index % 2 === 0 ? 0 : 0.6
            }

            // 选中：亮黄 + 黑色斜切标记。
            // 标记必须放在单元格左边距（spacingMd = 12）之内，否则会压住第一列的文字
            // ——之前画在 leftMargin: 6、宽 10 的位置，正好盖掉第一个字母。
            ZCutRect {
                anchors.fill: parent
                anchors.margins: 1
                visible: rowDelegate.isCurrent
                color: Theme.accent
                cutTopRight: Theme.cutSm
                cutBottomLeft: Theme.cutSm

                ZSkewRect {
                    anchors.left: parent.left
                    anchors.leftMargin: 2
                    anchors.verticalCenter: parent.verticalCenter
                    width: 5
                    height: 18
                    skew: 4
                    color: Theme.textOnAccent
                }
            }

            // 悬停：浅灰
            Rectangle {
                anchors.fill: parent
                anchors.margins: 1
                visible: rowDelegate.isHovered && !rowDelegate.isCurrent
                color: Theme.surfaceHover
            }

            // 行底分隔线
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.lineSoft
                opacity: 0.5
            }

            // 单元格
            Row {
                anchors.fill: parent
                Repeater {
                    model: root.columns
                    Item {
                        width: root._widths[index] || 0
                        height: rowDelegate.height

                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.leftMargin: Theme.spacingMd
                            anchors.rightMargin: Theme.spacingMd
                            anchors.verticalCenter: parent.verticalCenter
                            text: root.cellText(rowDelegate.rowData, modelData)
                            font: (modelData.mono === false) ? Theme.fontStrong : Theme.fontMonoMd
                            // 选中行底色是强调黄 → 文字用强调色上的文字色
                            color: rowDelegate.isCurrent ? Theme.textOnAccent : Theme.inkSoft
                            elide: Text.ElideRight
                            horizontalAlignment: modelData.align === "right" ? Text.AlignRight
                                             : modelData.align === "center" ? Text.AlignHCenter
                                             : Text.AlignLeft
                        }
                    }
                }
            }

            // 竖网格线：声明式绘制，选中态变化会自动换色
            Repeater {
                model: root.columns.length - 1
                Rectangle {
                    x: Math.round(root.xAt(index + 1))
                    width: 1
                    height: rowDelegate.height
                    color: rowDelegate.isCurrent ? Qt.rgba(0, 0, 0, 0.28) : Theme.lineSoft
                }
            }

            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                // 防止外层的 Flickable 把这次按下抢走当滚动手势（详见 rowClickRobust 说明）
                preventStealing: root.rowClickRobust
                onClicked: {
                    root.currentIndex = index
                    root.rowClicked(index)
                }
            }
        }
    }

    // ══ 底部装饰条 ══
    ZCutRect {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: Theme.dp(Theme.accentBarHeight)
        visible: root.footerText.length > 0
        color: Theme.accent
        cutBottomLeft: Theme.cutSm

        Text {
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacingMd + 4
            anchors.verticalCenter: parent.verticalCenter
            text: root.footerText
            font: Theme.fontMonoSm
            color: Theme.textOnAccent
        }

        Text {
            anchors.right: parent.right
            anchors.rightMargin: Theme.spacingMd + Theme.cutSm
            anchors.verticalCenter: parent.verticalCenter
            text: root.rows.length + " ROWS"
            font: Theme.fontMonoSm
            color: Theme.textOnAccent
            opacity: 0.7
        }
    }
}
