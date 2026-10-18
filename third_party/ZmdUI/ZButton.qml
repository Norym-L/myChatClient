import QtQuick
import QtQuick.Controls.Basic

/*!
    \qmltype ZButton
    \inqmlmodule ZmdUI
    \brief 工业机能风按钮：斜切角 + 平涂高对比 + 悬停反色。

    四种形态（\l variant 取 \c Theme.Variant 之一）：
    \list
    \li \c Primary   —— 荧光黄底 / 黑字 / 黑边；悬停整体反色为黑底黄字。
    \li \c Secondary —— 白底 / 黑字 / 黑边；悬停左侧亮黄竖条。
    \li \c Danger    —— 深炭底 / 白字；悬停时左侧出现亮黄竖条（不引入品红等点缀色，
                        点缀色只留给 ZStatusDot 这种极小面积的状态块）。
    \li \c Ghost     —— 无底无框，仅文字；悬停出现浅灰底 + 亮黄底线。
    \endlist

    \l skew > 0 时改用平行四边形外观（更适合做导航 / 分类按钮）。

    \qml
    ZButton { text: "一键配置并启动"; variant: Theme.Variant.Primary; onClicked: ... }
    ZButton { text: "扫描登录"; variant: Theme.Variant.Secondary; mono: "F2" }
    \endqml
*/
Button {
    id: control

    /*! 形态，取 Theme.Variant.* */
    property int variant: Theme.Variant.Primary
    /*! 右侧等宽小字：快捷键 / 编号 / 计数 */
    property string mono: ""
    /*! >0 时使用平行四边形外观，数值即倾斜量 */
    property real skew: 0
    /*! 斜切角大小，默认跟随主题的中号 */
    property int cutSize: Theme.cutMd
    /*! 悬停提示文案，非空时自动挂一个 ZToolTip（调用处不必再写 parent.hovered） */
    property string tooltip: ""

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    // 内边距恒定：悬停时不能改 padding。
    // 之前用 "spacingLg + (悬停 ? 6 : 0)" 给左侧竖条让位，结果鼠标一进按钮宽度就变，
    // 整行按钮跟着跳。竖条改成画在底板内部（下面 background 里左对齐、贴内边缘），谁都不动。
    leftPadding: Theme.spacingLg + 4
    rightPadding: Theme.spacingLg
    topPadding: Theme.spacingSm
    bottomPadding: Theme.spacingSm

    readonly property bool _isOn: hovered || visualFocus

    /*!
        左侧亮黄竖条是否显示。抽成纯函数有两个原因：
        \list
        \li 规则本身需要被测试覆盖，而离屏平台投递不了鼠标事件（实测连 mousePress
            都到不了 item），悬停态没法靠交互模拟出来 —— 只能直接单测规则。
        \li 把"什么情况下长竖条"这条设计规则写在一个地方，改的时候不用满文件找。
        \endlist
        \list
        \li 平行四边形（skew > 0）不画：竖条是直边，贴到斜边上会戳出形状外。
        \li Primary 不画：它的悬停反馈是整体反色（黄底黑字 → 黑底黄字），已经足够强。
        \li 其余形态（Secondary / Ghost / Danger）只在悬停或键盘聚焦时画，
            默认态干干净净，不做常显装饰。
        \endlist
    */
    function edgeVisibleFor(variantValue, skewAmount, active, isEnabled) {
        return isEnabled && skewAmount <= 0 && active && variantValue !== Theme.Variant.Primary
    }

    /*!
        左竖条的颜色。规则：**底色本身是亮黄时必须转黑**，否则竖条和底色同色等于没画
        （Secondary 悬停就是这种情况：白底变亮黄底）。
    */
    function edgeColorFor(variantValue, active) {
        // 底色是亮黄时竖条转"强调色上的文字色"（亮色=黑 / 暗色=深炭），
        // 不能用 Theme.ink —— 暗色下 ink 是浅灰，压在压暗后的黄上一样看不见
        return (variantValue === Theme.Variant.Secondary && active)
                ? Theme.textOnAccent : Theme.accent
    }

    readonly property bool showEdge: edgeVisibleFor(variant, skew, _isOn, enabled)
    readonly property color edgeColor: edgeColorFor(variant, _isOn)

    /*!
        底板的切角。**底板、Ghost 悬浮叠加层、焦点环共用这一份取值**。

        抽出来是因为这三块画的是"同一个形状"，各写各的迟早会漂移 —— Ghost 的悬浮底
        当初就是漏抄了这两个值，于是它退化成一块直角矩形、悬浮时直接戳出底板轮廓，
        表现成"别的按钮是六边形、只有『了解更多』悬浮时是方块"。改一处、三处同步，
        这个 bug 就不可能再出现。
    */
    readonly property real cutTopRightSize: enabled ? cutSize : 0
    readonly property real cutBottomLeftSize: enabled ? Theme.cutSm : 0

    /*! 悬停 / 聚焦的过渡时长。所有状态动画共用它，保证节奏一致。 */
    readonly property int stateDuration: Theme.durBase


    /*! 底面填充色 */
    readonly property color _fill: {
        if (!enabled)
            return Theme.surfaceAlt
        switch (variant) {
        case Theme.Variant.Primary:
            return _isOn ? Theme.ink : Theme.accent
        case Theme.Variant.Secondary:
            return _isOn ? Theme.accent : Theme.surface
        case Theme.Variant.Danger:
            return Theme.subBg
        default:
            // Ghost：底板恒为透明，"有没有底"交给下面那块独立叠加层
            return "transparent"
        }
    }

    /*! 描边色 */
    readonly property color _stroke: {
        if (!enabled)
            return Theme.lineSoft
        if (variant === Theme.Variant.Ghost)
            return "transparent"
        return Theme.ink
    }

    /*! 文字色 */
    readonly property color _fg: {
        if (!enabled)
            return Theme.muted
        if (variant === Theme.Variant.Primary)
            return _isOn ? Theme.accent : Theme.textOnAccent
        if (variant === Theme.Variant.Danger)
            return Theme.textOnSub
        return Theme.ink
    }

    implicitWidth: contentItem ? contentItem.implicitWidth + leftPadding + rightPadding : 120
    implicitHeight: Theme.dp(Theme.controlHeight)

    background: Item {
        // ── 底板：斜切角矩形 / 平行四边形 ──
        ZCutRect {
            anchors.fill: parent
            visible: control.skew <= 0
            color: control._fill
            strokeColor: control._stroke
            strokeWidth: control._stroke.a > 0 ? Theme.border : 0
            cutTopRight: control.cutTopRightSize
            cutBottomLeft: control.cutBottomLeftSize

            // 悬停/聚焦时底色平滑过渡（黄底黑字 ↔ 黑底黄字这类反色不再是硬切）
            Behavior on color {
                ColorAnimation { duration: control.stateDuration; easing.type: Theme.easingStd }
            }
        }

        ZSkewRect {
            anchors.fill: parent
            visible: control.skew > 0
            skew: control.skew
            color: control._fill
            strokeColor: control._stroke
            strokeWidth: control._stroke.a > 0 ? Theme.border : 0

            Behavior on color {
                ColorAnimation { duration: control.stateDuration; easing.type: Theme.easingStd }
            }
        }

        // ── Ghost 的悬浮底：独立叠加层，用 opacity 过渡 ──
        //
        // 为什么不让底板自己做 "transparent ↔ surfaceHover" 的颜色过渡：
        //   · 底板的 color 是四个形态共用的，而 Ghost 是唯一一个"底色靠 alpha=0
        //     表示不存在"的形态 —— "有没有底"和"底是什么颜色"被混在同一条通道上；
        //   · 这条颜色过渡和左竖条的宽度过渡挤在同一次状态变化里，状态一抖动
        //     动画就被反复重启，最先出问题的是它；
        //   · 挪到独立的不透明度通道后，悬浮底的显隐与底板颜色彻底解耦，
        //     任何形态的底色动画都影响不到它。
        ZCutRect {
            id: ghostHoverFill
            anchors.fill: parent
            visible: control.variant === Theme.Variant.Ghost
            color: Theme.surfaceHover
            opacity: control._isOn ? 1 : 0

            // 切角必须和底板一模一样：叠加层是"铺在底板上"的替身，
            // 少了这两个切角它就退化成一块直角矩形，悬浮时右上 / 左下会戳出底板轮廓外 ——
            // 用户看到的"了解更多悬浮是方角、别的按钮是六边形"就是这个。
            cutTopRight: control.cutTopRightSize
            cutBottomLeft: control.cutBottomLeftSize

            Behavior on opacity {
                NumberAnimation { duration: control.stateDuration; easing.type: Theme.easingStd }
            }
        }

        // ── 左侧亮黄竖条 ──
        //    画在底板内部（左内边缘），不参与布局，所以不会把按钮撑宽；
        //    宽度 0 → 5 用动画长出来，这就是"悬浮动画"的主体。
        ZCutRect {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.topMargin: 4
            anchors.bottomMargin: 4
            width: control.showEdge ? 5 : 0
            color: control.enabled ? control.edgeColor : Theme.lineSoft
            cutTopRight: 3
            cutBottomLeft: 3

            Behavior on width {
                NumberAnimation { duration: control.stateDuration; easing.type: Theme.easingStd }
            }
            // 竖条颜色跟着底色一起过渡：Secondary 白→黄的同时，竖条黄→黑
            Behavior on color {
                ColorAnimation { duration: control.stateDuration; easing.type: Theme.easingStd }
            }
        }

        // ── 键盘焦点环：向外偏 4px 的亮黄描边 ──
        ZCutRect {
            anchors.fill: parent
            anchors.margins: -4
            anchors.leftMargin: -1
            visible: control.visualFocus && control.enabled
            color: "transparent"
            strokeColor: Theme.accent
            strokeWidth: 2
            cutTopRight: control.cutTopRightSize
            cutBottomLeft: control.cutBottomLeftSize
        }
    }

    contentItem: Item {
        implicitWidth: row.implicitWidth
        implicitHeight: row.implicitHeight

        Row {
            id: row
            anchors.centerIn: parent
            // 按下时整体下沉 1px：机械按键的物理反馈
            anchors.horizontalCenterOffset: control.pressed ? 1 : 0
            anchors.verticalCenterOffset: control.pressed ? 1 : 0
            spacing: Theme.spacingSm

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: control.text
                font: Theme.fontStrong
                color: control._fg
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: control.mono.length > 0
                text: control.mono
                font: Theme.fontMonoSm
                color: control._fg
                opacity: 0.75
            }
        }
    }

    // 悬停提示：文案非空时自动挂一个 ZToolTip（调用处不必再写 parent.hovered）。
    //
    // ⚠️ 这里**不能自己绑 visible**。ToolTip 的显隐状态机在 C++ 的 T.ToolTip 里
    //    （它管 hovered + delay + timeout 三件事），一旦在外面把 visible 绑上，
    //    等于把整套逻辑覆盖掉：提示框会"一碰就弹"（delay: 300 形同虚设），
    //    而且弹层出现时抓走鼠标、可能反过来扰动宿主按钮的 hovered，
    //    表现就是"背景闪一下又变回白色"。
    ZToolTip {
        text: control.tooltip
    }
}
