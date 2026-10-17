import QtQuick
import QtQuick.Controls.Basic

/*!
    \qmltype ZRichTextEditor
    \inqmlmodule ZmdUI
    \brief 富文本编辑器：文字 / 图片 / 文件三类内容块混排，自带格式工具栏与序列化接口。

    文档是一个**块数组**（\l blocks），每块是纯 JS 对象，三种形态：

    \list
    \li \c text  —— 文字块（正文 / 标题 / 引用 / 代码 + 粗体 / 斜体 / 下划线 / 删除线 / 对齐）
    \li \c image —— 图片块（等比缩放 + 名称 + 说明 + 加载失败时的工程占位图）
    \li \c file  —— 文件块（文件名 + 大小 + 扩展名徽标，点击发出 \l fileActivated）
    \endlist

    \qml
    ZRichTextEditor {
        width: 640
        placeholderText: "输入正文…"
        onImageRequested: imagePicker.open()
        onFileRequested:  filePicker.open()

        Component.onCompleted: {
            insertTextBlock(-1, "终端工业机能风富文本")
            insertImageBlock("file:///C:/shots/board.png", "board.png", "面板截图")
            insertFileBlock("file:///C:/out/config.json", "config.json", 12800)
        }
    }
    \endqml

    \section1 为什么是"块"而不是"字符级富文本"

    Qt Quick 的 \c TextEdit 只提供**整段**的 \c font 属性，没有从 QML 侧给选区上
    \c QTextCharFormat 的接口（\c textDocument 暴露的是 QObject，QTextCursor /
    QTextCharFormat 都不在 QML 里）。纯 QML 想做「选中几个字加粗」就必须自己实现
    文本排版与光标。本库坚持纯 QML、零第三方依赖，所以把"富"做在**块**这一级：
    每块可以有自己的字体、字号、装饰、对齐，块与块之间混排图文文件 —— 这样既拿到了
    真正的富文本排版能力，又不需要编译器 / C++ 侧支持。

    \section1 为什么编辑器自己不做滚动

    不内置 Flickable，内容多高就长多高，由宿主决定怎么滚。这样避免了两个有名的问题：
    嵌套滚动（内层抢走滚轮，手感黏）和拖选被抢（父 Flickable 在拖拽超过阈值后接管，
    拖选文字拖到一半变成滚页面）。放进 \c ScrollView / 宿主的 \c Flickable / \c ZListView
    里都行，见演示程序。
*/
Item {
    id: editor

    /*! 块的形态枚举。也接受等价的字符串（"body" / "heading" / "quote" / "code"）。 */
    enum BlockStyle { Body, Heading, Quote, Code }

    // ════════════════════════════════════════════════════════════════
    //  1. 公共属性
    // ════════════════════════════════════════════════════════════════

    /*!
        文档内容：块对象数组。**替换**它才会触发界面重建；修改单个块的字段
        （比如用户打字）是原地改，不会重建、不会丢光标。

        每块的字段见 \l makeTextBlock / \l makeImageBlock / \l makeFileBlock。
    */
    property var blocks: []

    /*!
        内容版本号。每次内容变动自增。

        \note 块是**纯 JS 对象**，字段被原地改了不会发属性变更信号。所有需要跟随
        内容刷新的界面绑定都挂在 \l revision 上，靠它强制重算。自己的代码没必要碰它。
    */
    property int revision: 0

    /*! 当前聚焦 / 选中的块下标，-1 表示没有 */
    /*! 当前块下标（-1 = 没有当前块，例如空文档）。工具栏的格式开关作用在它身上。 */
    property int currentIndex: 0

    /*! 只读模式：隐藏工具栏，文字块不可编辑 */
    property bool readOnly: false

    /*! 是否画外框（放进 ZPanel 这类已经有卡片的容器里时关掉，避免双层边框） */
    property bool framed: true

    /*! 是否显示工具栏 */
    property bool showToolbar: true

    /*! 是否显示顶部深色标题条 */
    property bool showHeader: true

    /*! 标题条上的等宽小字 */
    property string headerText: "RICH TEXT / COMPOSER"

    /*! 空文字块的占位提示 */
    property string placeholderText: "输入正文…"

    /*! 图片块的最大显示高度（等比缩放，超出就按这个高度裁容器） */
    property int maxImageHeight: 240

    /*! 内容块之间的垂直间距 */
    property int blockSpacing: Theme.spacingSm

    /*! 内容变动（打字、增删块、改格式）时发出 */
    signal contentsChanged()

    /*! 用户点了工具栏的「图片」——宿主该打开文件选择框，选完调用 imageBlockAt / insertImageBlock */
    signal imageRequested()

    /*! 用户点了工具栏的「文件」——同上，选完调用 insertFileBlock */
    signal fileRequested()

    /*! 文件块被点击（双击或点击徽标），宿主可以打开 / 定位这个文件 */
    signal fileActivated(string url, string name)

    /*! 某个块被点击选中 */
    signal blockActivated(int index)

    /*! 内部：重建后要抢焦点的文字块下标（-1 = 不抢） */
    property int _focusRequest: -1

    implicitWidth: Theme.dp(560)
    implicitHeight: content.height

    // ════════════════════════════════════════════════════════════════
    //  2. 纯函数：块工厂 / 归一化 / 序列化
    //     全部不碰界面状态，可以直接在 qmltestrunner 里断言。
    // ════════════════════════════════════════════════════════════════

    /*! 造一个文字块。\a style 传字符串或 \l BlockStyle 枚举都行。 */
    function makeTextBlock(text, style) {
        return {
            type: "text",
            text: text === undefined || text === null ? "" : String(text),
            style: styleNameOf(style),
            bold: false,
            italic: false,
            underline: false,
            strike: false,
            align: "left"
        }
    }

    /*! 造一个图片块。\a caption 是图下的说明文字。 */
    function makeImageBlock(url, name, caption) {
        return {
            type: "image",
            url: url ? String(url) : "",
            name: name ? String(name) : "",
            caption: caption ? String(caption) : ""
        }
    }

    /*! 造一个文件块。\a size 是字节数，界面按 B/KB/MB 自动换算。 */
    function makeFileBlock(url, name, size) {
        return {
            type: "file",
            url: url ? String(url) : "",
            name: name ? String(name) : "未命名文件",
            size: Math.max(0, Math.round(Number(size) || 0))
        }
    }

    /*!
        把任意来路不明的对象补成合法块。JSON 反序列化、外部赋值都走这里，
        所以下游只可能见到三种形态、字段齐全的块，不用到处写防御性判断。
    */
    function normalizeBlock(raw) {
        if (!raw || typeof raw !== "object")
            return makeTextBlock("")

        var type = String(raw.type || "text").toLowerCase()

        if (type === "image")
            return {
                type: "image",
                url: raw.url ? String(raw.url) : "",
                name: raw.name ? String(raw.name) : "",
                caption: raw.caption ? String(raw.caption) : ""
            }

        if (type === "file")
            return {
                type: "file",
                url: raw.url ? String(raw.url) : "",
                name: raw.name ? String(raw.name) : "未命名文件",
                size: Math.max(0, Math.round(Number(raw.size) || 0))
            }

        var block = makeTextBlock(raw.text === undefined || raw.text === null ? "" : raw.text,
            raw.style)
        block.bold = !!raw.bold
        block.italic = !!raw.italic
        block.underline = !!raw.underline
        block.strike = !!raw.strike
        block.align = alignNameOf(raw.align)
        return block
    }

    /*! 枚举 / 字符串 → 规范的样式名。 */
    function styleNameOf(v) {
        if (typeof v === "string") {
            var s = v.toLowerCase()
            return _styleNames.indexOf(s) >= 0 ? s : "body"
        }
        var n = Number(v)
        return (n >= 0 && n < _styleNames.length) ? _styleNames[n] : "body"
    }

    /*! 对齐方式归一化。 */
    function alignNameOf(v) {
        var s = v ? String(v).toLowerCase() : "left"
        return (s === "center" || s === "right") ? s : "left"
    }

    /*! 样式名 → 中文短标签（工具栏、无障碍都用它）。 */
    function styleLabelOf(v) {
        switch (styleNameOf(v)) {
            case "heading":
                return "标题"
            case "quote":
                return "引用"
            case "code":
                return "代码"
            default:
                return "正文"
        }
    }

    /*! 字节数 → 人读的大小。0 或非法值一律 "0 B"。 */
    function formatBytes(n) {
        var v = Number(n)
        if (!isFinite(v) || v <= 0)
            return "0 B"
        var units = ["B", "KB", "MB", "GB", "TB"]
        var i = 0
        while (v >= 1024 && i < units.length - 1) {
            v /= 1024
            ++i
        }
        return (i === 0 ? String(Math.round(v)) : v.toFixed(1)) + " " + units[i]
    }

    /*! 文件名 → 大写扩展名徽标，没有扩展名给 "FILE"。 */
    function extensionOf(name) {
        var s = name ? String(name) : ""
        var dot = s.lastIndexOf(".")
        if (dot < 0 || dot === s.length - 1)
            return "FILE"
        var ext = s.substring(dot + 1).toUpperCase()
        return ext.length > 5 ? ext.substring(0, 5) : ext
    }

    /*! 取第 \a index 块，越界给 null。 */
    function blockAt(index) {
        return (index >= 0 && index < blocks.length) ? blocks[index] : null
    }

    /*! 只读快照：某个文字块的格式（\l revision 变化后所有调用者会重算）。 */
    function formatOf(index) {
        var b = blockAt(index)
        if (!b)
            return {
                type: "text", style: "body", bold: false, italic: false,
                underline: false, strike: false, align: "left"
            }
        return {
            type: b.type || "text",
            style: styleNameOf(b.style),
            bold: !!b.bold,
            italic: !!b.italic,
            underline: !!b.underline,
            strike: !!b.strike,
            align: b.align || "left"
        }
    }

    /*! 某类块的数量。 */
    function countBlocks(type) {
        var n = 0
        for (var i = 0; i < blocks.length; ++i)
            if ((blocks[i].type || "text") === type)
                ++n
        return n
    }

    /*! 各类块的统计快照（挂 revision，供标题条上的读数绑定）。 */
    readonly property var stats: {
        var r = revision
        return {
            rev: r,
            text: countBlocks("text"),
            image: countBlocks("image"),
            file: countBlocks("file")
        }
    }

    /*! 文档是否为空（没有块，或者只有空文字块）。 */
    readonly property bool isEmpty: {
        var r = revision
        if (blocks.length === 0)
            return true
        for (var i = 0; i < blocks.length; ++i) {
            var b = blocks[i]
            if ((b.type || "text") !== "text")
                return false
            if (b.text && b.text.length > 0)
                return false
        }
        return true
    }

    /*!
        纯文本导出：文字块逐行输出，图片 / 文件块各自折成一行占位标记。
        用于复制到剪贴板、喂给只认纯文本的接口。
    */
    function plainTextOf(list) {
        var arr = list || blocks
        var lines = []
        for (var i = 0; i < arr.length; ++i) {
            var b = arr[i]
            var t = b.type || "text"
            if (t === "image")
                lines.push("[图片" + (b.name ? ": " + b.name : "") + "]")
            else if (t === "file")
                lines.push("[文件" + (b.name ? ": " + b.name : "") + "]")
            else
                lines.push(b.text || "")
        }
        return lines.join("\n")
    }

    /*! 文档 → JSON 字符串。 */
    function toJSON() {
        return JSON.stringify(blocks)
    }

    /*! JSON 字符串 → 块数组。解析失败返回空数组，不会抛。 */
    function blocksFromJSON(text) {
        var raw = null
        try {
            raw = JSON.parse(text)
        } catch (e) {
            return []
        }
        if (!raw || raw.length === undefined)
            return []
        var out = []
        for (var i = 0; i < raw.length; ++i)
            out.push(normalizeBlock(raw[i]))
        return out
    }

    /*! 每个文字块该用的字体（把块格式翻成 QQmlFont）。 */
    function textFontFor(fmt) {
        var style = fmt ? styleNameOf(fmt.style) : "body"
        var family = style === "code" ? Theme.familyMono : Theme.family
        var pixel = style === "heading" ? 24 : style === "code" ? 13 : 14
        var weight = style === "heading" ? Font.Black : (fmt && fmt.bold ? Font.Bold : Font.Normal)
        return Qt.font({
            family: family,
            pixelSize: pixel,
            weight: weight,
            italic: (fmt && fmt.italic) || style === "quote",
            underline: !!(fmt && fmt.underline),
            strikeout: !!(fmt && fmt.strike),
            letterSpacing: style === "heading" ? 1 : 0
        })
    }

    /*! 文字块的水平对齐 → 对齐枚举。 */
    function hAlignFor(align) {
        var a = alignNameOf(align)
        // 用 Text.Align* 而不是 TextEdit.Align*：两者取值相同，
        // 但 Text 的这几个枚举更基础，不会因为控件类型换实现而找不到。
        return a === "center" ? Text.AlignHCenter
            : a === "right" ? Text.AlignRight
                : Text.AlignLeft
    }

    // 枚举值与样式名的对照表。靠"枚举按声明顺序从 0 开始"这条 QML 保证，
    // 不写自引用的 ZRichTextEditor.BlockStyle.Xxx，避免类型自引用。
    readonly property var _styleNames: ["body", "heading", "quote", "code"]

    // ════════════════════════════════════════════════════════════════
    //  3. 结构操作
    // ════════════════════════════════════════════════════════════════

    /*! 整体替换内容。非法项会被 \l normalizeBlock 补成合法块。 */
    function setBlocks(list) {
        var out = []
        if (list)
            for (var i = 0; i < list.length; ++i)
                out.push(normalizeBlock(list[i]))
        blocks = out
        currentIndex = out.length > 0 ? 0 : -1
        ++revision
        contentsChanged()
    }

    /*!
        在 \a afterIndex 后面插入一块，返回新块下标。
        \a afterIndex 传 -1 表示插到最前面；传 \c blocks.length 等价插到末尾。
    */
    function insertBlock(afterIndex, block) {
        var list = blocks.slice()
        var at = Math.max(0, Math.min(list.length, Number(afterIndex) + 1))
        var normalized = normalizeBlock(block)

        // 先登记再赋值：Repeater 是同步重建的，delegate 的 Component.onCompleted
        // 会在 blocks = list 这一句里就跑起来，晚一步登记就抢不到焦点。
        if (normalized.type === "text")
            _focusRequest = at

        list.splice(at, 0, normalized)
        blocks = list
        currentIndex = at
        ++revision
        contentsChanged()
        return at
    }

    /*! 在末尾追加（工具栏 / 「继续输入」都走这里）。 */
    function appendBlock(block) {
        return insertBlock(blocks.length - 1, block)
    }

    /*! 插入文字块。\a afterIndex 缺省为当前块。 */
    function insertTextBlock(afterIndex, text, style) {
        var at = afterIndex === undefined ? _targetIndex() : afterIndex
        return insertBlock(at, makeTextBlock(text || "", style))
    }

    /*! 插入图片块。\a afterIndex 缺省为当前块。 */
    function insertImageBlock(url, name, caption, afterIndex) {
        var at = afterIndex === undefined ? _targetIndex() : afterIndex
        return insertBlock(at, makeImageBlock(url, name, caption))
    }

    /*! 插入文件块。\a afterIndex 缺省为当前块。 */
    function insertFileBlock(url, name, size, afterIndex) {
        var at = afterIndex === undefined ? _targetIndex() : afterIndex
        return insertBlock(at, makeFileBlock(url, name, size))
    }

    /*! 删除一块。成功返回 true。 */
    function removeBlock(index) {
        var i = Number(index)
        if (!(i >= 0 && i < blocks.length))
            return false
        var list = blocks.slice()
        list.splice(i, 1)
        blocks = list
        currentIndex = list.length === 0 ? -1 : Math.min(i, list.length - 1)
        ++revision
        contentsChanged()
        return true
    }

    /*! 上下移动一块（内容区拖拽排序的按钮版）。越界或原地不动返回 false。 */
    function moveBlock(from, to) {
        var a = Number(from), b = Number(to)
        if (!(a >= 0 && a < blocks.length) || !(b >= 0 && b < blocks.length) || a === b)
            return false
        var list = blocks.slice()
        var moved = list.splice(a, 1)[0]
        list.splice(b, 0, moved)
        blocks = list
        currentIndex = b
        ++revision
        contentsChanged()
        return true
    }

    /*! 设置当前块的样式。非文字块会被忽略，返回 false。 */
    function setStyle(style) {
        var b = blockAt(_targetIndex())
        if (!b || (b.type || "text") !== "text")
            return false
        b.style = styleNameOf(style)
        ++revision
        contentsChanged()
        return true
    }

    /*! 反转当前文字块的某个布尔格式（"bold" / "italic" / "underline" / "strike"）。 */
    function toggleFormat(flag) {
        var b = blockAt(_targetIndex())
        if (!b || (b.type || "text") !== "text")
            return false
        if (["bold", "italic", "underline", "strike"].indexOf(flag) < 0)
            return false
        b[flag] = !b[flag]
        ++revision
        contentsChanged()
        return true
    }

    /*! 设置当前文字块的对齐。 */
    function setAlign(align) {
        var b = blockAt(_targetIndex())
        if (!b || (b.type || "text") !== "text")
            return false
        b.align = alignNameOf(align)
        ++revision
        contentsChanged()
        return true
    }

    /*! 清空为「只有一个空文字块」的初始状态。 */
    function clear() {
        blocks = [makeTextBlock("")]
        currentIndex = 0
        _focusRequest = 0
        ++revision
        contentsChanged()
    }

    /*! 当前操作的块下标：优先 \l currentIndex，没有就落在最后一块。 */
    function _targetIndex() {
        if (currentIndex >= 0 && currentIndex < blocks.length)
            return currentIndex
        return blocks.length - 1
    }

    /*! 内容被 delegate 反向写回时用：只自增版本号 + 发信号，不重建。 */
    function _contentEdited() {
        ++revision
        contentsChanged()
    }

    // ════════════════════════════════════════════════════════════════
    //  4. 版面：外框 → 标题条 → 工具栏 → 内容区
    // ════════════════════════════════════════════════════════════════

    // ── 外框 ──
    ZCutRect {
        anchors.fill: parent
        visible: editor.framed
        color: Theme.surface
        strokeColor: Theme.line
        strokeWidth: Theme.border
        cutTopRight: Theme.cutLg
        cutBottomLeft: Theme.cutMd
    }

    // 外框右上角的小装饰：斜线块，呼应面板的语言
    ZHatchPattern {
        visible: editor.framed
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: Theme.border
        anchors.topMargin: Theme.border
        width: Theme.cutLg * 3
        height: 26
        lineColor: Theme.hatch
        spacing: Theme.hatchStep
    }

    Column {
        id: content
        x: editor.framed ? Theme.border : 0
        y: editor.framed ? Theme.border : 0
        width: parent.width - (editor.framed ? Theme.border * 2 : 0)

        // ── 标题条：深色底 + 等宽读数 ──
        Item {
            id: headerRow
            width: parent.width
            height: editor.showHeader ? 30 : 0
            visible: editor.showHeader

            ZCutRect {
                anchors.fill: parent
                color: Theme.inkBlock
                cutTopRight: editor.framed ? Theme.cutLg - 2 : 0
            }

            Row {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingSm

                Rectangle {
                    width: 4
                    height: 12
                    color: Theme.accent
                }

                Text {
                    text: editor.headerText
                    font: Theme.fontMonoSm
                    color: Theme.textOnSub
                }

                Text {
                    text: "·"
                    font: Theme.fontMonoSm
                    color: Theme.alpha(Theme.textOnSub, 0.35)
                }

                Text {
                    text: editor.readOnly ? "READ ONLY" : "EDITING"
                    font: Theme.fontMonoSm
                    color: editor.readOnly ? Theme.accentGlow : Theme.alpha(Theme.textOnSub, 0.7)
                }
            }

            // 右侧读数：块数 / 图 / 文件
            Text {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingMd + (editor.framed ? Theme.cutMd : 0)
                anchors.verticalCenter: parent.verticalCenter
                text: editor.pad2(editor.stats.text) + " TXT  /  "
                    + editor.pad2(editor.stats.image) + " IMG  /  "
                    + editor.pad2(editor.stats.file) + " FILE"
                font: Theme.fontMonoSm
                color: Theme.alpha(Theme.textOnSub, 0.55)
            }
        }

        // ── 工具栏 ──
        Item {
            id: toolbarRow
            width: parent.width
            height: (editor.showToolbar && !editor.readOnly)
                ? Math.max(toolbarFlow.implicitHeight, moveBar.height) + Theme.spacingSm * 2 : 0
            visible: editor.showToolbar && !editor.readOnly

            Rectangle {
                anchors.fill: parent
                color: Theme.surfaceMuted
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.lineSoft
            }

            Flow {
                id: toolbarFlow
                x: Theme.spacingMd
                y: Theme.spacingSm
                // 给右侧固定的「上移 / 下移 / 删除」留位置，免得它们被挤到第二行
                width: parent.width - Theme.spacingMd * 3 - moveBar.width
                spacing: 3

                // ── 块样式 ──
                ToolChip {
                    caption: "正文"; active: editor._styleNow() === "body"
                    tooltip: "正文段落"; onClicked: editor.setStyle("body")
                }
                ToolChip {
                    caption: "标题"; active: editor._styleNow() === "heading"
                    tooltip: "大标题"; onClicked: editor.setStyle("heading")
                }
                ToolChip {
                    caption: "引用"; active: editor._styleNow() === "quote"
                    tooltip: "引用块（左侧粗竖线）"; onClicked: editor.setStyle("quote")
                }
                ToolChip {
                    caption: "代码"; active: editor._styleNow() === "code"
                    tooltip: "等宽代码块"; onClicked: editor.setStyle("code")
                }

                ToolDivider {
                }

                // ── 字型 ──
                ToolChip {
                    caption: "B"; mono: true; bold: true; active: editor._flagNow("bold")
                    tooltip: "加粗"; onClicked: editor.toggleFormat("bold")
                }
                ToolChip {
                    caption: "I"; mono: true; italic: true; active: editor._flagNow("italic")
                    tooltip: "斜体"; onClicked: editor.toggleFormat("italic")
                }
                ToolChip {
                    caption: "U"; mono: true; active: editor._flagNow("underline")
                    tooltip: "下划线"; onClicked: editor.toggleFormat("underline")
                }
                ToolChip {
                    caption: "S"; mono: true; active: editor._flagNow("strike")
                    tooltip: "删除线"; onClicked: editor.toggleFormat("strike")
                }

                ToolDivider {
                }

                // ── 对齐 ──
                ToolChip {
                    caption: "左"; active: editor._alignNow() === "left"
                    tooltip: "左对齐"; onClicked: editor.setAlign("left")
                }
                ToolChip {
                    caption: "中"; active: editor._alignNow() === "center"
                    tooltip: "居中"; onClicked: editor.setAlign("center")
                }
                ToolChip {
                    caption: "右"; active: editor._alignNow() === "right"
                    tooltip: "右对齐"; onClicked: editor.setAlign("right")
                }

                ToolDivider {
                }

                // ── 插入 ──
                ToolChip {
                    caption: "+ 图片"; active: false
                    tooltip: "插入图片块"; onClicked: editor.imageRequested()
                }
                ToolChip {
                    caption: "+ 文件"; active: false
                    tooltip: "插入文件块"; onClicked: editor.fileRequested()
                }
                ToolChip {
                    caption: "+ 文字"; active: false
                    tooltip: "在下方新起一段"; onClicked: editor.insertTextBlock(undefined, "")
                }
            }

            // 块顺序 / 删除：贴右边固定，不参与换行 —— 它们作用于「当前块」，
            // 混在样式芯片里会被误读成样式开关。
            Row {
                id: moveBar
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingMd
                y: Theme.spacingSm
                spacing: 3

                ToolChip {
                    caption: "↑"; mono: true; enabled: editor.currentIndex > 0
                    tooltip: "当前块上移"
                    onClicked: editor.moveBlock(editor.currentIndex, editor.currentIndex - 1)
                }
                ToolChip {
                    caption: "↓"; mono: true
                    enabled: editor.currentIndex >= 0 && editor.currentIndex < editor.blocks.length - 1
                    tooltip: "当前块下移"
                    onClicked: editor.moveBlock(editor.currentIndex, editor.currentIndex + 1)
                }
                ToolChip {
                    caption: "×"; mono: true; tone: "danger"
                    tooltip: "删除当前块"
                    onClicked: editor.removeBlock(editor.currentIndex)
                }
            }
        }

        // ── 内容区 ──
        Item {
            id: docArea
            width: parent.width
            height: docColumn.height + Theme.spacingMd

            // 空白文档时铺一层极淡的网格，提示"这里是可编辑区"
            ZGridBackground {
                anchors.fill: parent
                visible: editor.isEmpty
                opacity: 0.45
                // 必须显式透明：ZGridBackground 默认会用 bgColor 平涂一层底，
                // 直接放在卡片里会把卡片底色盖成 #F2F2F2（暗色主题下尤其难看）
                bgColor: "transparent"
                minorColor: Theme.gridLine
                majorColor: Theme.gridLineMajor
                crossColor: Theme.lineSoft
                minorStep: Theme.gridMinor
                majorStep: Theme.gridMajor
            }

            Column {
                id: docColumn
                x: Theme.spacingMd
                y: Theme.spacingSm
                width: docArea.width - Theme.spacingMd * 2
                spacing: editor.blockSpacing

                Repeater {
                    model: editor.blocks

                    delegate: Loader {
                        id: blockLoader

                        required property int index
                        required property var modelData

                        width: docColumn.width
                        // Loader 继承 QQuickImplicitSizeItem，implicitWidth/implicitHeight 只读，
                        // 不能直接赋值（写了编译报 read-only），所以用可写的 height 去接
                        // delegate 根的隐式高度。
                        // Loader.item 的静态类型是 QObject，读 implicitHeight 会报
                        // missing-property；运行时读到的确实是 Item，断言收窄是对的。
                        // qmllint disable missing-property
                        height: item instanceof Item ? item.implicitHeight : 0

                        sourceComponent: {
                            var t = modelData ? (modelData.type || "text") : "text"
                            if (t === "image")
                                return imageDelegate
                            if (t === "file")
                                return fileDelegate
                            return textDelegate
                        }

                        onLoaded: {
                            item.blockIndex = blockLoader.index
                            item.blockData = blockLoader.modelData
                        }
                    }
                }

                // 「继续输入」：空白文档或末尾都能一键新起一段
                Item {
                    width: docColumn.width
                    // 只读时没有"继续输入"这回事；但如果整篇是空的，还是要留一行说明，
                    // 否则只读 + 空文档 = 一条标题栏下面什么都没有。
                    height: (editor.readOnly && editor.blocks.length > 0) ? 0 : 34
                    visible: height > 0

                    MouseArea {
                        anchors.fill: parent
                        enabled: !editor.readOnly
                        hoverEnabled: true
                        cursorShape: Qt.IBeamCursor
                        onClicked: editor.insertTextBlock(editor.blocks.length - 1, "")
                        onEntered: addLabel.color = Theme.ink
                        onExited: addLabel.color = Theme.muted
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 1
                        color: Theme.lineSoft
                        opacity: 0.5
                    }

                    Text {
                        id: addLabel
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        text: editor.blocks.length > 0 ? "+ 点击继续输入"
                            : editor.readOnly ? "空文档 / NO CONTENT"
                                : "+ 点击开始输入"
                        font: Theme.fontMonoSm
                        color: Theme.muted
                    }
                }
            }
        }
    }

    // ════════════════════════════════════════════════════════════════
    //  5. 内部小工具
    // ════════════════════════════════════════════════════════════════

    function pad2(n) {
        return (n < 10 ? "0" : "") + n
    }

    // 工具栏的高亮取值。读 revision 让它跟着内容刷新。
    function _styleNow() {
        var r = revision
        var b = blockAt(_targetIndex())
        return b ? styleNameOf(b.style) : "body"
    }

    function _flagNow(flag) {
        var r = revision
        var b = blockAt(_targetIndex())
        return b ? !!b[flag] : false
    }

    function _alignNow() {
        var r = revision
        var b = blockAt(_targetIndex())
        return b ? alignNameOf(b.align) : "left"
    }

    // ── 工具栏芯片：选中 = 亮黄底黑字，悬停 = 悬停底 ──
    component ToolChip: Button {
        id: chip

        property string caption: ""
        property bool active: false
        property bool mono: false
        property bool bold: false
        property bool italic: false
        property string tone: ""          // "" 普通 / "danger" 危险
        property string tooltip: ""

        // 芯片窄一点：工具栏一行要放下 12 个芯片 + 右侧 3 个操作，
        // 用 spacingMd 做内边距就会把「+ 文字」挤到第二行。
        implicitWidth: Math.max(26, chipLabel.implicitWidth + Theme.spacingSm * 2)
        implicitHeight: 24
        padding: 0
        hoverEnabled: true
        text: ""

        background: ZCutRect {
            anchors.fill: parent
            color: !chip.enabled ? "transparent"
                : chip.active ? (chip.tone === "danger" ? Theme.danger : Theme.accent)
                    : chip.hovered ? Theme.surfaceHover
                        : "transparent"
            strokeColor: chip.active ? "transparent"
                : chip.hovered ? Theme.lineSoft : "transparent"
            strokeWidth: 1
            cutTopRight: Theme.cutSm
            cutBottomLeft: 3

            Behavior on color {
                ColorAnimation {
                    duration: Theme.durFast; easing.type: Theme.easingStd
                }
            }
        }

        contentItem: Text {
            id: chipLabel
            text: chip.caption
            // 一个 font 绑定写完所有子属性。
            // 不能既写 `font: ...` 又写 `font.bold: ...`：那是对同一属性重复绑定，
            // 静态检查会报 duplicate-property-binding，运行时后者还会静默盖掉前者的绑定。
            // 注意：注释行不要以 qmllint 开头，否则会被当成 lint 指令解析。
            font: Qt.font({
                family: chip.mono ? Theme.familyMono : Theme.family,
                pixelSize: chip.mono ? 13 : 14,
                bold: chip.bold,
                italic: chip.italic
            })
            color: !chip.enabled ? Theme.muted
                : chip.active ? (chip.tone === "danger" ? Theme.textOnSub : Theme.textOnAccent)
                    : Theme.ink
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        // 只给 text，不碰 visible —— 可见性归 ToolTip 自己的 delay/timeout 状态机
        ZToolTip {
            text: chip.tooltip
        }
    }

    component ToolDivider: Rectangle {
        width: 1
        height: 14
        color: Theme.lineSoft
    }

    // ════════════════════════════════════════════════════════════════
    //  6. 文字块
    // ════════════════════════════════════════════════════════════════

    Component {
        id: textDelegate

        Item {
            id: tRoot

            property int blockIndex: -1
            property var blockData: null

            // Loader.onLoaded 才把 blockData 补进来（晚于 delegate 的 onCompleted），
            // 所以在这里接住它，把文字灌进 TextArea。
            onBlockDataChanged: if (area) area._syncFromBlock()

            // 抢焦点必须等 blockIndex 有了真值再判：delegate 的 Component.onCompleted
            // 阶段 blockIndex 还是 -1，而 _focusRequest 的"没有请求"哨兵恰好也是 -1，
            // 在 onCompleted 里判相等会让**每个**文字块都抢一次焦点，最后一个赢 ——
            // 表现为窗口刚显示时末块被选中、工具栏高亮跳到最后。
            onBlockIndexChanged: {
                if (tRoot.blockIndex >= 0 && editor._focusRequest === tRoot.blockIndex) {
                    editor._focusRequest = -1
                    if (area)
                        area.forceActiveFocus()
                }
            }

            // 唯一的格式依赖点：revision 一变，fmt 重新求值（新对象 → 属性通知 → 下面全部重算）
            readonly property var fmt: {
                var r = editor.revision
                return editor.formatOf(tRoot.blockIndex)
            }

            readonly property bool isHeading: fmt.style === "heading"
            readonly property bool isQuote: fmt.style === "quote"
            readonly property bool isCode: fmt.style === "code"
            readonly property bool selected: editor.currentIndex === tRoot.blockIndex

            // 文字相对块左边缘的缩进：给标题的黄竖条、引用的灰竖条让位。
            // 代码块没有竖条，靠 leftPadding 内缩，所以 leftInset 是 0。
            readonly property int leftInset: isHeading ? 10 : isQuote ? 14 : 0
            readonly property int leftPad: isCode ? Theme.spacingMd : 0
            readonly property int topInset: isHeading ? Theme.spacingSm
                : (isQuote || isCode) ? Theme.spacingSm : 2
            readonly property int bottomInset: isHeading ? 4 : (isQuote || isCode) ? Theme.spacingSm : 2

            width: parent ? parent.width : 0
            implicitHeight: area.height + topInset + bottomInset

            // 引用 / 代码块的底
            ZCutRect {
                anchors.fill: parent
                visible: tRoot.isQuote || tRoot.isCode
                color: tRoot.isCode ? Theme.surfaceSunken : Theme.surfaceMuted
                strokeColor: tRoot.isCode ? Theme.lineSoft : "transparent"
                strokeWidth: tRoot.isCode ? 1 : 0
                cutTopRight: tRoot.isCode ? Theme.cutSm : 0
                cutBottomLeft: tRoot.isCode ? 4 : 0
            }

            // 左侧竖条：标题用亮黄，引用用深灰粗线
            Rectangle {
                visible: tRoot.isHeading || tRoot.isQuote
                x: 0
                y: Theme.spacingXs
                width: tRoot.isHeading ? 4 : 3
                height: Math.max(0, tRoot.height - Theme.spacingXs * 2)
                color: tRoot.isHeading ? Theme.accent : Theme.lineSoft
            }

            // 聚焦时的整块轮廓
            ZCutRect {
                anchors.fill: parent
                anchors.margins: -2
                visible: tRoot.selected && area.activeFocus
                color: "transparent"
                strokeColor: Theme.accent
                strokeWidth: 1
                cutTopRight: Theme.cutSm
                cutBottomLeft: 4
                opacity: 0.9
            }

            TextArea {
                id: area

                x: tRoot.leftInset
                y: tRoot.topInset
                width: tRoot.width - tRoot.leftInset
                height: contentHeight + topPadding + bottomPadding

                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                background: null
                padding: 0
                leftPadding: tRoot.leftPad
                rightPadding: Theme.spacingSm
                topPadding: 0
                bottomPadding: 0

                readOnly: editor.readOnly
                selectByMouse: true
                // 块编辑器里 Tab 应该归文本自己用（缩进），不该把块塞进窗口的 Tab 焦点链，
                // 否则 Tab 一次跳一个块，正文里的 Tab 就没法用了。
                activeFocusOnTab: false

                font: editor.textFontFor(tRoot.fmt)
                color: tRoot.isQuote ? Theme.inkSoft : Theme.ink
                horizontalAlignment: editor.hAlignFor(tRoot.fmt.align)
                selectionColor: Theme.accent
                selectedTextColor: Theme.textOnAccent
                placeholderText: tRoot.blockIndex === 0 ? editor.placeholderText : ""
                placeholderTextColor: Theme.muted

                // 首次装载 / 外部整体替换后同步；用户打字不回灌到 text（避免绑成环丢光标）
                Component.onCompleted: _syncFromBlock()

                // blockData 是 Loader 在 onLoaded 里补上的 —— 那是**晚于**本项
                // Component.onCompleted 的时刻，所以首次装载必须在 onLoaded 之后
                // 再同步一次，否则文字块永远是空的（图片/文件块因为是绑定所以没事）。
                function _syncFromBlock() {
                    var raw = tRoot.blockData ? tRoot.blockData.text : ""
                    var t = (raw === undefined || raw === null) ? "" : String(raw)
                    if (area.text !== t)
                        area.text = t
                }

                onTextChanged: {
                    if (!tRoot.blockData || tRoot.blockData.text === area.text)
                        return
                    tRoot.blockData.text = area.text
                    editor._contentEdited()
                }

                onActiveFocusChanged: {
                    if (activeFocus) {
                        editor.currentIndex = tRoot.blockIndex
                        editor.blockActivated(tRoot.blockIndex)
                    }
                }
            }
        }
    }

    // ════════════════════════════════════════════════════════════════
    //  7. 图片块
    // ════════════════════════════════════════════════════════════════

    Component {
        id: imageDelegate

        Item {
            id: iRoot

            property int blockIndex: -1
            property var blockData: null

            readonly property bool selected: editor.currentIndex === iRoot.blockIndex
            readonly property string _url: iRoot.blockData ? (iRoot.blockData.url || "") : ""
            readonly property string _name: iRoot.blockData ? (iRoot.blockData.name || "") : ""
            readonly property string _caption: iRoot.blockData ? (iRoot.blockData.caption || "") : ""

            // 图片原始宽高比；还没加载到就给 16:10 兜底，避免首帧高度为 0 抖动
            readonly property real _ratio: (img.implicitWidth > 0 && img.implicitHeight > 0)
                ? (img.implicitWidth / img.implicitHeight) : 1.6
            readonly property real frameHeight: {
                var w = iRoot.width > 0 ? iRoot.width : editor.implicitWidth
                return Math.max(Theme.dp(96), Math.min(editor.maxImageHeight, w / _ratio))
            }

            readonly property bool _showPlaceholder: img.status !== Image.Ready

            width: parent ? parent.width : 0
            implicitHeight: frameHeight + (_caption.length > 0 ? 24 : 0)

            ZCutRect {
                id: imageFrame
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: iRoot.frameHeight
                color: Theme.surfaceSunken
                strokeColor: iRoot.selected ? Theme.accent : Theme.lineSoft
                strokeWidth: iRoot.selected ? 2 : 1
                cutTopRight: Theme.cutLg
                cutBottomLeft: Theme.cutSm

                Image {
                    id: img
                    anchors.fill: parent
                    anchors.margins: 2
                    source: iRoot._url
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    cache: true
                    smooth: true
                }

                // ── 占位图：没选图 / 加载失败时的工程图样式 ──
                Item {
                    anchors.fill: parent
                    anchors.margins: 2
                    visible: iRoot._showPlaceholder

                    ZHatchPattern {
                        anchors.fill: parent
                        lineColor: Theme.hatch
                        spacing: Theme.hatchStep
                        cross: true
                    }

                    // 四角瞄准框
                    Repeater {
                        model: 4
                        delegate: ZCutRect {
                            required property int index
                            width: 14
                            height: 14
                            color: "transparent"
                            strokeColor: Theme.muted
                            strokeWidth: 1
                            x: index % 2 === 0 ? 6 : parent.width - width - 6
                            y: index < 2 ? 6 : parent.height - height - 6
                            cutTopRight: index === 1 || index === 2 ? 10 : 0
                            cutBottomLeft: index === 1 || index === 2 ? 10 : 0
                        }
                    }

                    Column {
                        anchors.centerIn: parent
                        spacing: Theme.spacingXs

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: iRoot._url.length > 0 ? "IMAGE UNAVAILABLE" : "NO IMAGE"
                            font: Theme.fontMonoMd
                            color: Theme.muted
                        }

                        Text {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: iRoot._name.length > 0 ? iRoot._name : "点工具栏「+ 图片」插入"
                            font: Theme.fontMonoSm
                            color: Theme.muted
                            opacity: 0.8
                        }
                    }
                }

                // 左上角 IMG 徽标
                ZTag {
                    anchors.left: parent.left
                    anchors.leftMargin: Theme.spacingSm
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spacingSm
                    text: "IMG"
                    tone: Theme.Tone.Ink
                }

                // 右上角删除
                ToolChip {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.spacingSm
                    anchors.top: parent.top
                    anchors.topMargin: Theme.spacingSm
                    caption: "×"
                    mono: true
                    tone: "danger"
                    tooltip: "移除这张图"
                    onClicked: editor.removeBlock(iRoot.blockIndex)
                }

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        editor.currentIndex = iRoot.blockIndex
                        editor.blockActivated(iRoot.blockIndex)
                    }
                }
            }

            // 图下说明
            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: imageFrame.bottom
                anchors.topMargin: 5
                visible: iRoot._caption.length > 0
                text: iRoot._caption
                font: Theme.fontMonoSm
                color: Theme.muted
                elide: Text.ElideRight
            }
        }
    }

    // ════════════════════════════════════════════════════════════════
    //  8. 文件块
    // ════════════════════════════════════════════════════════════════

    Component {
        id: fileDelegate

        Item {
            id: fRoot

            property int blockIndex: -1
            property var blockData: null

            readonly property bool selected: editor.currentIndex === fRoot.blockIndex
            readonly property string _url: fRoot.blockData ? (fRoot.blockData.url || "") : ""
            readonly property string _name: fRoot.blockData ? (fRoot.blockData.name || "") : ""
            readonly property var _size: fRoot.blockData ? (fRoot.blockData.size || 0) : 0

            width: parent ? parent.width : 0
            implicitHeight: 52

            ZCutRect {
                id: fileFrame
                anchors.fill: parent
                color: fileHover.hovered ? Theme.surfaceHover : Theme.surface
                strokeColor: fRoot.selected ? Theme.accent : Theme.lineSoft
                strokeWidth: fRoot.selected ? 2 : 1
                cutTopRight: Theme.cutMd
                cutBottomLeft: 4

                Behavior on color {
                    ColorAnimation {
                        duration: Theme.durFast; easing.type: Theme.easingStd
                    }
                }
            }

            // 左侧扩展名徽标
            ZCutRect {
                id: extBadge
                x: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                width: 40
                height: 40
                color: Theme.subBg
                cutTopRight: 8
                cutBottomLeft: 3

                Text {
                    anchors.centerIn: parent
                    text: editor.extensionOf(fRoot._name)
                    font: Qt.font({family: Theme.familyMono, pixelSize: 11, bold: true})
                    color: Theme.textOnSub
                }
            }

            // 文件名 + 元信息
            Column {
                anchors.left: extBadge.right
                anchors.leftMargin: Theme.spacingMd
                anchors.right: removeChip.left
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                spacing: 3

                Text {
                    width: parent.width
                    text: fRoot._name.length > 0 ? fRoot._name : "未命名文件"
                    font: Theme.fontStrong
                    color: Theme.ink
                    elide: Text.ElideMiddle
                }

                Text {
                    width: parent.width
                    text: editor.formatBytes(fRoot._size) + "  ·  ATTACHMENT"
                    font: Theme.fontMonoSm
                    color: Theme.muted
                    elide: Text.ElideRight
                }
            }

            ToolChip {
                id: removeChip
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingSm
                anchors.verticalCenter: parent.verticalCenter
                caption: "×"
                mono: true
                tone: "danger"
                tooltip: "移除附件"
                onClicked: editor.removeBlock(fRoot.blockIndex)
            }

            HoverHandler {
                id: fileHover
            }

            // 点击区只覆盖「徽标 + 文件名」这一段，右边留出删除按钮的地盘。
            // 如果让 TapHandler 铺满整行，点 × 会先删块、再触发 fileActivated(已删除的块)。
            Item {
                anchors.left: parent.left
                anchors.right: removeChip.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom

                TapHandler {
                    acceptedButtons: Qt.LeftButton
                    onTapped: {
                        editor.currentIndex = fRoot.blockIndex
                        editor.blockActivated(fRoot.blockIndex)
                        editor.fileActivated(fRoot._url, fRoot._name)
                    }
                }
            }
        }
    }
}
