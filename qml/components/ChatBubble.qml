import QtQuick
import ZmdUI 1.0

/*!
    一条消息气泡。

    两种朝向：
      · 对方（fromSelf = false）：头像在左，白底黑边气泡
      · 自己（fromSelf = true） ：头像在右，亮黄底黑边气泡（和主按钮同一套配色）

    \l state 只影响"这条到底发出去没有"的标注，不改变版式：
      demo   演示数据，不标注
      local  只在本机（服务端还没实现文本消息）
      sent   服务端已接收（等接口通了这个状态才有意义）
      failed 发送失败

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

    /*!
        投递状态的等宽小字。demo / sent 不显示 —— 前者是假数据，
        后者在服务端有回执之前根本没机会出现，显示了反而是噪声。
    */
    component StateMark: Text {
        visible: root.state !== "demo" && root.state !== "sent" && root.state.length > 0
        text: root.state === "local" ? "仅本地" : (root.state === "failed" ? "发送失败" : root.state)
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

                Text {
                    text: root.time
                    font: Theme.fontMonoSm
                    color: Theme.muted
                    opacity: 0.7
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

                Text {
                    text: root.time
                    font: Theme.fontMonoSm
                    color: Theme.muted
                    opacity: 0.7
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
}
