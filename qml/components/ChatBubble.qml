import QtQuick
import ZmdUI 1.0

/*!
    一条消息气泡。

    两种朝向：
      · 对方（fromSelf = false）：头像在左，白底黑边气泡
      · 自己（fromSelf = true） ：头像在右，亮黄底黑边气泡（和主按钮同一套配色）

    \l state 只影响"这条到底发出去没有"的标注，不改变版式：
      demo     演示数据，不标注
      sending  已发给服务端、等回执（1014）
      sent     服务端已接收（只代表写进了对方离线消息流，不是"已读"）
      failed   发送失败（含超时 / 连接断开）
      local    演示会话里的本机回显（没发往服务器）

    \note 自己的头像由页面通过 selfName / selfUid 传进来，组件不直接读控制器 ——
          保持"展示组件只认 property"这条边界。
*/
Item {
    id: root

    /*! true = 我发出的 */
    property bool fromSelf: false
    /*! 消息正文 */
    property string text: ""
    /*! 时间，原样显示 */
    property string time: ""
    /*! 投递状态（见文件头） */
    property string state: "demo"
    /*! 对方昵称 / uid（气泡旁的头像与名字用） */
    property string peerName: ""
    property string peerUid: ""
    /*! 自己的昵称 / uid（只在 fromSelf 时用到） */
    property string selfName: ""
    property string selfUid: ""

    /*! 选择模式：整行可点，点一下切换选中 */
    property bool selectable: false
    /*! 是否已选中（只在 selectable 时有意义） */
    property bool selected: false
    /*! 选择模式下点了这一行 */
    signal toggleRequested()

    /*!
        右键点了这一行（要开操作菜单）。

        a x / a y 是相对 \l coordinateSpace 的坐标 —— 气泡本身不知道
        页面在哪，所以由 delegate 把页面根节点传进来换算菜单位置。
    */
    signal contextMenuRequested(real x, real y)

    /*! 坐标参照物（页面根节点），右键菜单按它定位 */
    property Item coordinateSpace: null

    /*!
        是否显示"这条是什么时候发的"。

        默认藏起来：每条都挂一个 HH:mm，一整屏看下来全是重复的两位数，
        把消息本身压下去了。鼠标在气泡上停满 1 秒才显形（QQ 的做法），
        移开就收回去 —— 需要精确时间的时候停一下就有，平时不占视线。
    */
    property bool showTime: false

    readonly property int avatarSize: 28
    readonly property int hPad: 12
    readonly property int vPad: 8
    /// 气泡最宽占整行 72%：再宽就和对面贴上了，看不出是谁说的
    readonly property real maxBubbleWidth: Math.round(root.width * 0.72)

    /// 用 TextMetrics 量自然宽度，而不是读 Text.implicitWidth ——
    /// 后者在弯折状态下会和"气泡宽度"互相依赖，直接构成绑定循环。
    TextMetrics {
        id: metrics
        font: Theme.fontBody
        text: root.text
    }

    readonly property real bubbleWidth: Math.min(metrics.advanceWidth + hPad * 2 + 1,
        maxBubbleWidth)

    implicitHeight: root.fromSelf ? selfRow.implicitHeight : otherRow.implicitHeight
    width: parent ? parent.width : 400

    /*
     * 鼠标在气泡上停满 1 秒，才把"这条是什么时候发的"亮出来。
     *
     * 用 restart() 而不是 start()：在相邻消息之间划过去时，每进一条都重新计时，
     * 不会因为"只划过 0.3 秒"就闪一下时间。移开立刻收回去。
     */
    HoverHandler {
        id: hoverHandler

        onHoveredChanged: {
            if (hoverHandler.hovered) {
                hoverTimer.restart()
            } else {
                hoverTimer.stop()
                root.showTime = false
            }
        }
    }

    Timer {
        id: hoverTimer
        interval: 1000
        repeat: false
        onTriggered: root.showTime = true
    }

    /*!
        投递状态的等宽小字。demo / sent 不显示 —— 前者是假数据，
        后者是"正常成功"，每条都挂个「已发送」纯属噪声（失败才值得占位置）。
    */
    component StateMark: Text {
        visible: root.state !== "demo" && root.state !== "sent" && root.state.length > 0
        text: root.state === "sending" ? "发送中…"
            : (root.state === "failed" ? "发送失败"
            : (root.state === "local" ? "仅本地" : root.state))
        font: Theme.fontMonoSm
        color: root.state === "failed" ? Theme.danger : Theme.muted
    }

    // ══════════════════ 对方 ══════════════════
    Row {
        id: otherRow
        visible: !root.fromSelf
        width: parent.width
        spacing: Theme.spacingSm

        ZAvatar {
            size: root.avatarSize
            name: root.peerName
            uid: root.peerUid
        }

        // 内容块：先"名字 + 时间"一行，下面挂气泡
        Item {
            width: Math.max(otherMeta.implicitWidth, root.bubbleWidth)
            height: otherMeta.implicitHeight + 4 + otherBubble.height

            Row {
                id: otherMeta
                anchors.left: parent.left
                anchors.top: parent.top
                spacing: Theme.spacingSm

                Text {
                    text: root.peerName.length > 0 ? root.peerName : root.peerUid
                    font: Theme.fontMonoSm
                    color: Theme.muted
                }

                /*
                 * 时间：默认藏着（占着位、透明），悬浮满 1s 才浮出来。
                 * 用 opacity 而不是 visible —— visible 切换会让后面那个
                 * StateMark 左右跳一下，看着像消息在抖。
                 */
                Text {
                    text: root.time
                    font: Theme.fontMonoSm
                    color: Theme.muted
                    opacity: root.showTime ? 0.85 : 0

                    Behavior on opacity {
                        NumberAnimation {
                            duration: Theme.durFast
                            easing.type: Theme.easingStd
                        }
                    }
                }

                StateMark {
                }
            }

            Item {
                id: otherBubble
                anchors.left: parent.left
                anchors.top: otherMeta.bottom
                anchors.topMargin: 4
                width: root.bubbleWidth
                height: Math.max(20, otherBody.implicitHeight) + root.vPad * 2

                // 右上 + 左下切角，形状指向左上的头像
                ZCutRect {
                    anchors.fill: parent
                    color: Theme.surface
                    strokeColor: Theme.line
                    strokeWidth: Theme.border
                    cutTopRight: 10
                    cutBottomLeft: 4
                }

                Text {
                    id: otherBody
                    x: root.hPad
                    y: root.vPad
                    width: parent.width - root.hPad * 2
                    text: root.text
                    wrapMode: Text.Wrap
                    font: Theme.fontBody
                    color: Theme.ink
                    lineHeight: 1.25
                }
            }
        }
    }

    // ══════════════════ 自己 ══════════════════
    Row {
        id: selfRow
        visible: root.fromSelf
        anchors.right: parent.right
        spacing: Theme.spacingSm

        // 内容块：右对齐（时间 + 状态在气泡上方右侧）
        Item {
            width: Math.max(selfMeta.implicitWidth, root.bubbleWidth)
            height: selfMeta.implicitHeight + 4 + selfBubble.height

            Row {
                id: selfMeta
                anchors.right: parent.right
                anchors.top: parent.top
                spacing: Theme.spacingSm

                StateMark {
                }

                // 自己的时间同样默认藏着，悬浮 1s 才显形（对齐对方那条的行为）
                Text {
                    text: root.time
                    font: Theme.fontMonoSm
                    color: Theme.muted
                    opacity: root.showTime ? 0.85 : 0

                    Behavior on opacity {
                        NumberAnimation {
                            duration: Theme.durFast
                            easing.type: Theme.easingStd
                        }
                    }
                }
            }

            Item {
                id: selfBubble
                anchors.right: parent.right
                anchors.top: selfMeta.bottom
                anchors.topMargin: 4
                width: root.bubbleWidth
                height: Math.max(20, selfBody.implicitHeight) + root.vPad * 2

                // 左上 + 右下切角，形状指向右上的头像
                ZCutRect {
                    anchors.fill: parent
                    /*
                     * 自己那条用反相块（深黑底白字），不用亮黄。
                     * 理由：亮黄在这个设计语言里是"可交互 / 已选中"的信号
                     * （侧栏选中项、主按钮、未读角标都在用它），而一条条气泡是**内容**。
                     * 大面积亮黄铺在消息区，会把"能点的地方"这个语义冲淡。
                     */
                    color: Theme.inkBlock
                    strokeColor: Theme.ink
                    strokeWidth: Theme.border
                    cutTopLeft: 10
                    cutBottomRight: 4
                }

                Text {
                    id: selfBody
                    x: root.hPad
                    y: root.vPad
                    width: parent.width - root.hPad * 2
                    text: root.text
                    wrapMode: Text.Wrap
                    font: Theme.fontBody
                    color: Theme.textOnSub
                    lineHeight: 1.25
                }
            }
        }

        ZAvatar {
            size: root.avatarSize
            // 未登录 / 拿不到昵称时别显示成 "?"，直接写"我"
            name: root.selfName.length > 0 ? root.selfName : "我"
            uid: root.selfUid
        }
    }

    // ══════════════════ 选择模式 ══════════════════
    /*
     * 选中态只画在整行外围（描边 + 淡淡一层底色），不去动气泡本身的版式 ——
     * 挑消息的时候还要能看清每条说的是什么。
     *
     * 试过再加一个圆形「✓」角标：不管放哪都会压在"昵称 + 时间"那一行上，
     * 反而不如这一圈高亮清楚（底部操作条还会显示已选条数）。
     */
    Rectangle {
        anchors.fill: parent
        visible: root.selectable && root.selected
        color: Theme.accentSoft
        opacity: 0.35
        border.width: 2
        border.color: Theme.accent
        radius: 4
    }

    /*
     * 整行点击切换选中。**只有选择模式下才启用** ——
     * enabled=false 的 MouseArea 不消费事件，普通模式下的排版/滚动不受影响。
     */
    MouseArea {
        anchors.fill: parent
        enabled: root.selectable
        cursorShape: Qt.PointingHandCursor
        onClicked: root.toggleRequested()
    }

    /*
     * 右键：消息操作菜单（复制 / 删除 / 将来的表情包）。
     *
     * 只收右键 —— acceptedButtons 限定成 RightButton 之后，左键事件仍然
     * 落到下面的 Flickable（拖拽滚动）和上面那个选择模式 MouseArea，
     * 不会因为多了一层 MouseArea 就把滚动吃掉。
     */
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.RightButton
        enabled: !root.selectable

        onClicked: (mouse) => {
            const space = root.coordinateSpace !== null ? root.coordinateSpace : root
            const spot = root.mapToItem(space, mouse.x, mouse.y)
            root.contextMenuRequested(spot.x, spot.y)
        }
    }
}
