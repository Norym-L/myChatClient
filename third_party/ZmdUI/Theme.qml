pragma Singleton

import QtQuick
import QtQuick.Controls

/*!
    \qmltype Theme
    \inqmlmodule ZmdUI
    \brief 终末地工业机能风 · 全局主题单例（配色 / 字体 / 尺寸 / 动画）

    所有组件都只从这里取值，不写任何魔法数字。切换主题只需改 \l mode：

    \code
    Theme.mode = Theme.Mode.Dark    // 暗色
    Theme.mode = Theme.Mode.Light   // 浅色（默认）
    \endcode

    \note 尺寸单位一律是「逻辑像素」。Qt 6 会按 devicePixelRatio 自动放大到物理像素，
          所以本文件里不需要也不应该出现 dpr 乘法；\l dp() 只做「设计密度」换算与取整，
          避免在高 DPI 下出现 0.5 px 导致的边缘发虚。
*/
QtObject {
    id: theme

    // ════════════════════════════════════════════════════════════════
    //  1. 主题模式
    // ════════════════════════════════════════════════════════════════

    /*! 主题枚举：Light = 0，Dark = 1（对应 \l mode 的取值） */
    enum Mode { Light, Dark }

    /*! 当前主题。默认浅色：亮白底 + 荧光黄 + 纯黑。 */
    property int mode: 0                        // Theme.Mode.Light
    readonly property bool isDark: mode === 1   // Theme.Mode.Dark

    // ════════════════════════════════════════════════════════════════
    //  2. 语义枚举（组件的 variant / tone 统一在这里定义，避免各写各的）
    // ════════════════════════════════════════════════════════════════

    /*! 按钮等交互控件的形态 */
    enum Variant { Primary, Secondary, Danger, Ghost }

    /*! 标签 / 状态点 / 强调块的语义色调 */
    enum Tone { Ink, Accent, Outline, Muted, Cyan, Magenta, Danger }

    // ════════════════════════════════════════════════════════════════
    //  3. 配色（严格遵循设计规范，避免大面积渐变）
    //
    //  表面分四层，暗色下靠"层级"而不是"亮边框"来区分结构：
    //      sunken（凹陷：轨道 / 凹槽） < bg（页面） < muted（中间层） < surface（抬升：卡片）
    //  亮色主题里 surface 是最亮的（白卡片压在浅灰底上），
    //  暗色主题里 surface 也必须比 bg **更亮**，否则卡片会像"洞"而不是"板"。
    //  参考 https://www.fz.wiki/ 的令牌：#181818 / #1f1f1f / #232323 就是这个关系。
    // ════════════════════════════════════════════════════════════════

    /*! 页面主背景：亮白 / 浅灰 */
    readonly property color bg:            isDark ? "#181818" : "#F2F2F2"
    /*! 抬升面：卡片 / 面板 / 输入框 */
    readonly property color surface:       isDark ? "#232323" : "#FFFFFF"
    /*! 中间层：交替行、悬停底、次级卡片（旧名 surfaceAlt 保留兼容） */
    readonly property color surfaceMuted:  isDark ? "#1F1F1F" : "#E6E6E6"
    readonly property color surfaceAlt:   surfaceMuted
    /*! 凹陷面：轨道、凹槽、代码块 —— 比页面更暗，暗色下才有"凹进去"的层次 */
    readonly property color surfaceSunken: isDark ? "#101010" : "#E4E4E4"
    /*!
        悬停面。注意亮暗两个主题的方向是**相反**的：
        亮色卡是白的，悬停要变暗一档；暗色卡是 #232323，悬停必须变亮一档。
        写死 direction 的悬停色在暗色下会"越悬停越黑"，等于没有反馈。
    */
    readonly property color surfaceHover:  isDark ? "#2C2C2C" : "#E9E9E9"
    /*! 网格线（细） */
    readonly property color gridLine:      isDark ? "#242424" : "#E4E4E4"
    /*! 网格线（主刻度，略深） */
    readonly property color gridLineMajor: isDark ? "#303030" : "#CFCFCF"

    /*!
        主强调色：荧光黄 / 柠檬黄。高亮、装饰底边、选中态专用。

        \note 暗色下**必须压暗**（#E8FA00 → #D8BF00）：大面积亮柠檬黄浮在暗背景上
        会非常刺眼，而且亮黄自带"发光"错觉，压暗后反而更像工业屏上的涂层。
        参考站的令牌同样是 #f2cc00 → #d8bf00 这个关系。
    */
    readonly property color accent:        isDark ? "#D8BF00" : "#E8FA00"
    /*! 强调色的浅一档（亮色下用） */
    readonly property color accentSoft:    isDark ? "#272302" : "#DFFF00"
    /*! 强调色的提亮档：暗色下画细线 / 小字用，保证对比度 */
    readonly property color accentGlow:    isDark ? "#ECD548" : "#E8FA00"
    /*! 落在强调色上的文字（深色）。注意不要命名成 onXxx，那是信号处理器语法。 */
    readonly property color textOnAccent:  isDark ? "#161616" : "#000000"

    /*! 主文字：亮色纯黑，暗色浅灰（近白会发飘，用 #EEE 更稳） */
    readonly property color ink:           isDark ? "#EEEEEE" : "#000000"
    /*! 深炭色，比 ink 软一点，用于正文 */
    readonly property color inkSoft:       isDark ? "#D2D6DA" : "#1A1A1A"
    /*! 辅助文字 / 等宽说明文字。暗色下用冷灰，纯中性灰在暗底上会显脏 */
    readonly property color muted:         isDark ? "#A8B0B7" : "#5C5C5C"

    /*! 副标题底色：深灰（亮暗两套主题都保持"深色块 + 白字"这个身份） */
    readonly property color subBg:         isDark ? "#2E2E2E" : "#333333"
    /*! 副标题底色（更浅一档，用作分层） */
    readonly property color subBgAlt:      isDark ? "#3C3C3C" : "#4D4D4D"
    /*! 深灰底上的文字：纯白 */
    readonly property color textOnSub:     "#FFFFFF"
    /*!
        反相块（黑底白字那种条/徽标：表头、编号块、对话框标题带）。

        \note 不能用 \l ink 当它的底色 —— ink 在暗色下是浅灰，块会变成"浅底白字"，
        文字直接消失。反相块在两个主题里都必须是**深色**：亮色近黑，暗色比卡片更黑
        （#0F0F0F 压在 #232323 卡片上依然是明显的深色带）。
    */
    readonly property color inkBlock:      isDark ? "#0F0F0F" : "#111111"

    /*!
        结构线（边框、分隔线）。
        \note 暗色下**绝对不能用近白色**：我第一版把 line 反相成 #EFEFEF，
        每个卡片都镶一圈白边，夜里看非常刺眼，而且失去了"层级"的表达。
        正确做法是冷灰 #4A5158：既能描出轮廓，又不抢视线。
    */
    readonly property color line:          isDark ? "#4A5158" : "#000000"
    /*! 弱结构线 */
    readonly property color lineSoft:      isDark ? "#2E343A" : "#ADADAD"
    /*! 斜线纹理颜色 */
    readonly property color hatch:         isDark ? "#2E2E2E" : "#D6D6D6"
    /*! 斜线纹理颜色（加重档，用于对话框中央） */
    readonly property color hatchStrong:   isDark ? "#3C3C3C" : "#C0C0C0"

    /*! 点缀色 1：高饱和青（仅用于极小的数据状态块） */
    readonly property color cyan:         "#00E5FF"
    /*! 淡青色：黑底副标题块、深色带上的小装饰点。
        纯白点在这种 5px 级别的小方块上对比过强、边界发虚，淡青更清楚也更贴合设计语言。 */
    readonly property color cyanSoft:     "#6FE4F5"
    /*! 点缀色 2：品红（仅用于极小的数据状态块） */
    readonly property color magenta:      "#FF00FF"
    /*! 危险 / 异常状态（用深灰底 + 品红细边表达，不引入大面积红） */
    readonly property color danger:       isDark ? "#FF3D71" : "#C2003B"

    /*! 颜色加透明度的小工具：Theme.alpha(Theme.ink, 0.4) */
    function alpha(c, a) {
        return Qt.rgba(c.r, c.g, c.b, a)
    }

    // ════════════════════════════════════════════════════════════════
    //  4. 字体
    // ════════════════════════════════════════════════════════════════

    /*! 正文字族。Windows 上「微软雅黑」只有 Regular/Bold 两档，
        Font.Black(900) 会落到 Bold；标题想要更粗的观感请看 displayHeavy。 */
    readonly property string family: Qt.platform.os === "windows"
                                     ? "Microsoft YaHei UI" : "Noto Sans CJK SC"
    /*! 拉丁标题字族：Arial Black，用于英文全大写大字 */
    readonly property string familyDisplay: Qt.platform.os === "windows"
                                            ? "Arial Black" : "DejaVu Sans"
    /*! 等宽字族：SYSTEM INTERFACE / 坐标 / 编号一律用它 */
    readonly property string familyMono: Qt.platform.os === "windows"
                                         ? "Consolas" : "DejaVu Sans Mono"

    /*! 超大标题（中文带「」），配 displayHeavy 假粗体描边使用 */
    readonly property font fontTitle:    Qt.font({ family: family, pixelSize: 42,
                                                   weight: Font.Black, letterSpacing: 1 })
    /*! 英文全大写大标题 */
    readonly property font fontTitleEn:  Qt.font({ family: familyDisplay, pixelSize: 40,
                                                   weight: Font.Black, letterSpacing: 1 })
    /*! 区块标题 / 卡片标题 */
    readonly property font fontHeading:  Qt.font({ family: family, pixelSize: 22,
                                                   weight: Font.Black, letterSpacing: 1 })
    /*! 副标题（黑底白字那一块） */
    readonly property font fontSubtitle: Qt.font({ family: family, pixelSize: 19,
                                                   weight: Font.Black, letterSpacing: 2 })
    /*! 正文 */
    readonly property font fontBody:     Qt.font({ family: family, pixelSize: 14,
                                                   weight: Font.Normal })
    /*! 加重正文：按钮、列表项标签 */
    readonly property font fontStrong:   Qt.font({ family: family, pixelSize: 14,
                                                   weight: Font.DemiBold })
    /*! 数值大字（统计卡里的 US$9.7665） */
    readonly property font fontNumber:   Qt.font({ family: family, pixelSize: 30,
                                                   weight: Font.Black, letterSpacing: 1 })
    /*! 小标签，全大写加字距 */
    readonly property font fontLabel:    Qt.font({ family: family, pixelSize: 11,
                                                   weight: Font.DemiBold, letterSpacing: 1.5 })
    /*! 等宽小字（默认） */
    readonly property font fontMonoSm:   Qt.font({ family: familyMono, pixelSize: 11 })
    /*! 等宽中字 */
    readonly property font fontMonoMd:   Qt.font({ family: familyMono, pixelSize: 13 })

    /*!
        \note 关于"超粗"：曾经用 Text.Outline + 同色 styleColor 给中文标题加粗，
        但在 40px 级别下描边会把「概」「况」这类字的字腔糊死，反而看不清，已全部去掉。
        现在的做法是直接用 Font.Black（Windows 上落到微软雅黑 Bold）+ 收敛字距，
        要更粗请自行换字体（如思源黑体 Heavy）改 \l family 即可。
    */

    // ════════════════════════════════════════════════════════════════
    //  5. 尺寸与几何
    // ════════════════════════════════════════════════════════════════

    readonly property int spacingXs: 4
    readonly property int spacingSm: 8
    readonly property int spacingMd: 12
    readonly property int spacingLg: 20
    readonly property int spacingXl: 32

    /*! 斜切角尺寸：小（按钮/标签）、中（输入框/列表项）、大（卡片/对话框） */
    readonly property int cutSm: 6
    readonly property int cutMd: 12
    readonly property int cutLg: 22

    /*! 边框：常规 2px，重结构 3px */
    readonly property int border: 2
    readonly property int borderThick: 3

    /*! 控件高度 */
    readonly property int controlHeightSm: 28
    readonly property int controlHeight: 38
    readonly property int controlHeightLg: 46

    /*! 卡片底部装饰条高度 */
    readonly property int accentBarHeight: 22

    /*! 工程网格步长 */
    readonly property int gridMinor: 12
    readonly property int gridMajor: 60
    /*! 斜线纹理步长 */
    readonly property int hatchStep: 9

    /*! 设计密度：1.0 为标准；0.9 紧凑，1.15 宽松。会整体缩放所有控件高度与间距。 */
    property real density: 1.0

    /*! 尺寸换算：套用密度后取整，避免高 DPI 下的半像素模糊。 */
    function dp(v) {
        return Math.round(v * density)
    }

    // ════════════════════════════════════════════════════════════════
    //  6. 动画
    // ════════════════════════════════════════════════════════════════

    /*!
        光标闪烁的半周期（ms）。

        故意**跟随系统设置**而不是写死设计值（Windows 默认全周期 1060ms）——
        光标的节奏应该和系统里其它输入框一致，不然会显得"这个 App 的光标很奇怪"。
        系统把 cursorFlashTime 设为 0 表示"不闪烁"，这里兜一个下限，
        免得定时器退化成 0 间隔的忙等。
    */
    readonly property int caretFlashHalf: Math.max(200, Math.round(Application.styleHints.cursorFlashTime / 2))

    /*! 悬停 / 按下等即时反馈 */
    readonly property int durFast: 90
    /*! 默认过渡 */
    readonly property int durBase: 160
    /*! 展开 / 弹层 */
    readonly property int durSlow: 260

    readonly property int easingStd: Easing.OutCubic
    readonly property int easingEnter: Easing.OutBack
    readonly property int easingExit: Easing.InQuad
}
