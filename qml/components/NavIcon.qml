import QtQuick

/*!
    导航栏线性图标 —— 用 Canvas 手绘，不依赖任何图标字体。

    为什么不用字符：ZmdUI 的整体语汇是"等宽字符当图形"，但导航栏图标只有 20px，
    字符在不同字重下大小不一，而且 Consolas 缺字形时会掉成方框（☰、⚙ 这些就悬）。
    这里按 24×24 的坐标系画，线宽 / 尺寸 / 颜色都能跟着主题走。
*/
Item {
    id: root

    /*!
        图标类别：
        list
        li c chat      对话气泡（会话）
        li c contacts  人形（联系人）
        li c apply     人形 + 加号（好友申请）
        li c theme     太阳（外观 / 主题）
        li c logout    门 + 箭头（退出登录）
        endlist
    */
    property string kind: "chat"
    /*! 线条颜色（主题切换时重画，见 onColorChanged） */
    property color color: "#8C8C8C"

    implicitWidth: 20
    implicitHeight: 20

    onKindChanged: canvas.requestPaint()
    onColorChanged: canvas.requestPaint()

    Canvas {
        id: canvas
        anchors.fill: parent

        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)

            // 一律在 24×24 的坐标系里画，再缩放到实际尺寸 —— 线宽也跟着缩放，
            // 视觉粗细在任何尺寸下都一致。
            const s = Math.min(width, height) / 24
            ctx.save()
            ctx.scale(s, s)
            ctx.strokeStyle = root.color
            ctx.lineWidth = 2
            ctx.lineCap = "round"
            ctx.lineJoin = "round"
            root.draw(ctx)
            ctx.restore()
        }
    }

    /*! 画笔入口：按 kind 画不同的形状 */
    function draw(ctx) {
        if (root.kind === "chat") {
            // 对话气泡，左下带一个小尾巴
            ctx.beginPath()
            ctx.moveTo(5.5, 4)
            ctx.lineTo(18.5, 4)
            ctx.quadraticCurveTo(20.5, 4, 20.5, 6)
            ctx.lineTo(20.5, 13.5)
            ctx.quadraticCurveTo(20.5, 15.5, 18.5, 15.5)
            ctx.lineTo(12.5, 15.5)
            ctx.lineTo(7.5, 20)
            ctx.lineTo(7.5, 15.5)
            ctx.lineTo(5.5, 15.5)
            ctx.quadraticCurveTo(3.5, 15.5, 3.5, 13.5)
            ctx.lineTo(3.5, 6)
            ctx.quadraticCurveTo(3.5, 4, 5.5, 4)
            ctx.closePath()
            ctx.stroke()
        } else if (root.kind === "contacts") {
            // 头
            ctx.beginPath()
            ctx.arc(12, 8, 3.8, 0, Math.PI * 2)
            ctx.stroke()
            // 肩（半圆拱）
            ctx.beginPath()
            ctx.arc(12, 21, 7.2, Math.PI, Math.PI * 2)
            ctx.stroke()
        } else if (root.kind === "apply") {
            // 人形整体左移，右上留出加号的位置
            ctx.beginPath()
            ctx.arc(9.5, 8.5, 3.4, 0, Math.PI * 2)
            ctx.stroke()

            ctx.beginPath()
            ctx.arc(9.5, 21, 6.8, Math.PI, Math.PI * 2)
            ctx.stroke()

            ctx.beginPath()
            ctx.moveTo(19, 3.5)
            ctx.lineTo(19, 9.5)
            ctx.moveTo(16, 6.5)
            ctx.lineTo(22, 6.5)
            ctx.stroke()
        } else if (root.kind === "theme") {
            // 太阳：圆盘 + 八道短射线
            ctx.beginPath()
            ctx.arc(12, 12, 4.2, 0, Math.PI * 2)
            ctx.stroke()

            for (let i = 0; i < 8; ++i) {
                const a = i * Math.PI / 4
                ctx.beginPath()
                ctx.moveTo(12 + Math.cos(a) * 7.2, 12 + Math.sin(a) * 7.2)
                ctx.lineTo(12 + Math.cos(a) * 9.6, 12 + Math.sin(a) * 9.6)
                ctx.stroke()
            }
        } else if (root.kind === "logout") {
            // 门框（右侧只留半边）+ 穿门而出的箭头
            ctx.beginPath()
            ctx.moveTo(13.5, 4)
            ctx.lineTo(19.5, 4)
            ctx.lineTo(19.5, 20)
            ctx.lineTo(13.5, 20)
            ctx.stroke()

            ctx.beginPath()
            ctx.moveTo(4, 12)
            ctx.lineTo(14, 12)
            ctx.moveTo(10.5, 8.5)
            ctx.lineTo(14, 12)
            ctx.lineTo(10.5, 15.5)
            ctx.stroke()
        }
    }
}
