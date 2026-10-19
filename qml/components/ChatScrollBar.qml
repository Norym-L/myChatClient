import QtQuick
import ZmdUI 1.0

/*!
    聊天记录右侧滚动条。

    为什么不直接用 ZmdUI 的 ZScrollBar：那是 QtQuick.Controls 的 ScrollBar 皮肤，
    滚动范围取的是 Flickable.contentHeight。消息区曾经是 ListView，而 ListView 复用
    delegate 时 contentHeight 是**估算值**（实测同一份内容在 4446~21765 之间乱跳），
    照它算滑块拖到一半内容就"滚完了"，拖到底还会掉进最后一条下面那片空白。
    现在消息区换成了 Flickable + Column（高度是实数），这个问题从根上没了。

    ⚠ 拖动必须是**即时**的：contentY 直接跟着鼠标比例赋值。
      早先为了让 ListView 不出空白，做成了"每帧最多跨半屏的追帧"，结果滑块跟着
      鼠标走了、内容还在后面追 —— 手感是"不跟手"，松手那一刻内容才追到目标位置，
      看起来像闪一下。Column 的高度是实数，一跳到位既准又快，不需要追帧。

    样式沿用设计语言：6px 直角斜切滑块（右上 / 左下切角 4），
    常态 lineSoft → 悬停 accent → 拖动 ink。

    用法（挂在被滚视图的右外侧，让出视图的右边距，不压内容）：
    qml
    ChatScrollBar {
        anchors.top: list.top
        anchors.bottom: list.bottom
        anchors.left: list.right
        anchors.leftMargin: Theme.spacingXs
        width: 10
        target: list
    }
    endqml
*/
Item {
    id: root

    /*! 要控制的视图（Flickable / ListView / 带 contentY & contentHeight 的对象） */
    property var target: null

    /*! 可拖动的滑块最小高度，避免内容极长时滑块细成一条线 */
    property real minHandleHeight: 36
    /*! 滚轮一格滚多少像素 */
    property real wheelStep: 60

    readonly property bool interactive: !!target && maxY > 0.5 && height > 0
    readonly property real maxY: {
        if (!target)
            return 0
        // 这两个读数只为建立依赖：内容一动 / 总高重算，滑块就得跟着挪
        const content_pos = target.contentY
        const content_total = target.contentHeight
        if (typeof target.maxContentY === "function")
            return Math.max(0, target.maxContentY())
        return Math.max(0, content_total - target.height)
    }

    // 上下各缩 2px，让滑块在首尾时还能看出"到头了"
    readonly property real trackTop: 2
    readonly property real trackH: Math.max(0, height - trackTop * 2)
    readonly property real handleH: {
        if (!interactive)
            return 0
        // 视口占全部内容的比例 = 滑块占轨道的比例
        const viewport = target.height
        const total = viewport + maxY
        return Math.min(trackH, Math.max(minHandleHeight, trackH * viewport / Math.max(1, total)))
    }

    // ── 位置：拖动中跟着鼠标，其余时候跟着内容 ──
    property bool dragging: false
    property real dragRatio: 0

    readonly property real contentRatio: maxY > 0
                                             ? Math.max(0, Math.min(1, target.contentY / maxY)) : 0
    readonly property real ratio: dragging ? dragRatio : contentRatio

    readonly property real handleY: trackTop + Math.max(0, trackH - handleH) * ratio

    visible: interactive

    // ── 轨道：一条很淡的竖线，给滑块一个"能拖"的暗示 ──
    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        y: root.trackTop
        width: 1
        height: root.trackH
        color: Theme.lineSoft
        opacity: 0.5
        visible: root.interactive
    }

    // ── 滑块 ──
    ZCutRect {
        id: handle
        width: 6
        height: root.handleH
        x: Math.round((root.width - width) / 2)
        y: Math.round(root.handleY)
        color: dragArea.pressed ? Theme.ink : (dragArea.containsMouse ? Theme.accent : Theme.lineSoft)
        cutTopRight: 4
        cutBottomLeft: 4

        // 拖动中不插动画：滑块必须和手指在同一个像素上
        Behavior on y {
            enabled: !dragArea.pressed
            NumberAnimation { duration: Theme.durFast; easing.type: Easing.OutCubic }
        }
    }

    function clampY(value) {
        return Math.max(0, Math.min(maxY, value))
    }

    /// 把内容摆到某个比例位置（0 = 最顶，1 = 最底）
    function goToRatio(value) {
        if (!target)
            return
        const ratio = Math.max(0, Math.min(1, value))
        target.contentY = clampY(ratio * maxY)
    }

    /// 松手：把方向盘交还给页面（重新判定"在不在底部"、收掉越界）
    function finishScroll() {
        if (root.target && typeof root.target.endBarScroll === "function")
            root.target.endBarScroll()
    }

    // ── 交互：整条轨道都能点 / 拖（热区比 6px 滑块宽一点，好点中）──
    MouseArea {
        id: dragArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: root.interactive ? Qt.PointingHandCursor : Qt.ArrowCursor

        // 按下时鼠标相对滑块顶部的偏移，保证拖动时滑块不跳
        property real grabOffset: 0

        function ratioFor(mouse_y) {
            const travel = Math.max(1, root.trackH - root.handleH)
            return Math.max(0, Math.min(1, (mouse_y - root.trackTop - grabOffset) / travel))
        }

        onPressed: (mouse) => {
            if (!root.interactive)
                return
            if (root.target && typeof root.target.beginBarScroll === "function")
                root.target.beginBarScroll()
            // 点在滑块外：滑块先挪到点击处，再顺势滚过去
            const on_handle = mouse.y >= root.handleY && mouse.y <= root.handleY + root.handleH
            grabOffset = on_handle ? mouse.y - root.handleY : root.handleH / 2
            root.dragging = true
            root.dragRatio = ratioFor(mouse.y)
            root.goToRatio(root.dragRatio)
        }
        onPositionChanged: (mouse) => {
            if (!pressed || !root.interactive)
                return
            root.dragRatio = ratioFor(mouse.y)
            root.goToRatio(root.dragRatio)
        }
        onReleased: {
            if (!root.dragging)
                return
            root.dragging = false
            root.finishScroll()
        }
        onCanceled: {
            if (root.dragging)
                root.dragging = false
            root.finishScroll()
        }
        onWheel: (wheel) => {
            if (!root.interactive)
                return
            const step = (wheel.angleDelta.y / 120) * root.wheelStep
            root.target.contentY = root.clampY(root.target.contentY - step)
        }
    }
}
