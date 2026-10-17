import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Dialogs

/*!
    \qmltype ZChatInput
    \inqmlmodule ZmdUI
    \brief 聊天输入框：多行文本 + 图片 / 文件附件，Enter 发送、拖入即加附件。

    形制照着 IM 的消息输入框做：上面是**附件预览区**（图片缩略图、文件卡片，都可单独删掉），
    中间是可长高的多行输入区，底部一排工具（图片 / 文件 / 清空）与发送按钮。

    \qml
    ZChatInput {
        width: 560
        placeholderText: "输入消息…"
        onSendRequested: (text, attachments) => chat.send(text, attachments)
    }
    \endqml

    \section1 「图片」/「文件」按钮默认就能用

    点它们会弹出系统文件选择框（\c QtQuick.Dialogs），选完按扩展名分流成图片缩略图
    或文件卡片。宿主想换成自己的选择器（带预览、最近使用、远程目录……）时，
    把 \l useSystemDialogs 置 \c false，然后接 \l imagePickRequested /
    \l filePickRequested，选完再调 \l addImage / \l addFile / \l addUrlsAsAttachments。

    组件仍然不发网络请求、不决定发送后要不要清空：点「发送」只发出
    \l sendRequested，是否清空由 \l clearOnSend 或宿主自己决定。

    \section1 键盘约定

    \list
    \li 默认 \c Enter 发送、\c Shift+Enter 换行（\l sendOnEnter 可反转为「Enter 换行、Ctrl+Enter 发送」）
    \li \c Ctrl+Enter 在两种模式下都发送 —— 不用记当前是哪种模式
    \li \c Escape 发出 \l cancelRequested，宿主可用来收起 @ 候选、清空草稿等
    \endlist

    \section1 输入区的高度与滚动

    默认是**定高 + 内部滚动**（\l allowResize 为 \c false、\l allowOverflow 为 \c true）：
    输入区高度恒为 \l minTextHeight，写长了就在框内滚，右侧出现滚动条，光标始终可见。
    两个开关可以自由组合：

    \list
    \li \l allowResize：允许输入区跟着内容长高（\l minTextHeight → \l maxTextHeight）
    \li \l allowOverflow：允许正文超出输入区高度；关掉后，会溢出的那次输入直接被退回
    \endlist

    | allowResize | allowOverflow | 行为 |
    | --- | --- | --- |
    | false（默认） | true（默认） | 定高，超出部分内部滚动 + 滚动条 |
    | true | true | 先长高到 maxTextHeight，再内部滚动 |
    | false | false | 定高，写满就不接受更多输入 |
    | true | false | 长高到 maxTextHeight，写满就不接受更多输入 |

    这和 ZRichTextEditor 的"完全不滚动"不同 —— 聊天框固定在屏幕一角，
    不可能把整页撑高。
*/
Item {
    id: composer

    // ════════════════════════════════════════════════════════════════
    //  1. 数据
    // ════════════════════════════════════════════════════════════════

    /*! 草稿正文。直接读写输入区，不需要中间变量。 */
    property alias text: area.text

    /*!
        附件数组，元素形如 `{ type: "image" | "file", url, name, size, id }`。

        约定：**整体替换**才会刷新界面。组件内部增删附件一律用
        `attachments = attachments.slice()` 重新赋值，自己的代码也请照做，
        不要 `attachments.push(...)` —— 那改的是数组本身，绑定收不到通知。
    */
    property var attachments: []

    /*!
        附件版本号。附件数组被整体替换时自增，给"想在附件变化时做点事"
        的绑定一个稳定的依赖点（\l attachments 本身也能绑，这个只是方便）。
    */
    property int revision: 0

    // ════════════════════════════════════════════════════════════════
    //  2. 外观
    // ════════════════════════════════════════════════════════════════

    /*! 是否画外框（放进 ZPanel 这类已有卡片的容器时关掉，避免双层边框） */
    property bool framed: true

    /*! 是否显示底部工具栏 */
    property bool showToolbar: true

    /*! 是否显示顶部深色标题条 */
    property bool showHeader: true

    /*! 标题条上的等宽小字 */
    property string headerText: "MESSAGE / COMPOSER"

    /*! 空输入时的占位提示。默认空 —— 框里不放任何提示文字，要就自己给 */
    property string placeholderText: ""

    /*! 发送按钮文字 */
    property string sendText: "发送"

    /*! 附件区留空时要显示的一行提示。默认空 —— 没有附件时附件区高度就是 0，不占地方 */
    property string attachHintText: ""

    /*! 输入区最小高度。关掉 \l allowResize 后，输入区高度就恒等于它 */
    property int minTextHeight: 74

    /*! 输入区最大高度：开着 \l allowResize 时，输入区长到这个高度就不再长，改走内部滚动 */
    property int maxTextHeight: 140

    /*!
        允许输入区跟着内容变高（\l minTextHeight → \l maxTextHeight）。默认 \c false：
        高度恒定，写长了在框内滚动。想要 QQ 那种"越写越高、到顶再滚"就置 \c true。
    */
    property bool allowResize: false

    /*!
        允许正文超出输入区高度（超出后在输入区内部滚动，右侧出现滚动条）。默认 \c true。

        置 \c false 时，输入区高度就是可视上限：**会让内容溢出的那次输入会被整段退回**，
        并发 \l limitReached("height")；程序化赋一个超长 \l text 也会被退回上一次的内容。
    */
    property bool allowOverflow: true

    /*! 滚轮一格（120 角度增量）滚多少像素。触控板的像素增量不受它影响 */
    property int wheelStepSize: 48

    /*!
        输入区滚到上 / 下边界后，继续滚是否把剩余距离交给外层页面（浏览器式链式滚动）。
        默认 \c false：到头就停住，外层页面一动不动。

        注意内容**放得下**（没有可滚区域）时不适用这条 —— 那时组件不参与滚轮，
        事件照常落到外层，否则一行的输入框会变成页面滚动的死区。
    */
    property bool wheelChainsToAncestor: false

    /*! 图片缩略图边长 */
    property int thumbSize: 64

    /*! 文件卡片宽度 */
    property int fileCardWidth: 208

    /*! 文件卡片高度 */
    property int fileCardHeight: 56

    /*! 是否允许加图片附件（关掉后工具栏的「图片」按钮不出现） */
    property bool allowImage: true

    /*! 是否允许加文件附件 */
    property bool allowFile: true

    /*! 只读：输入区不可编辑、工具栏与发送按钮禁用 */
    property bool readOnly: false

    /*! 发送中：发送按钮换成忙碌外观并禁用，输入区仍可继续写 */
    property bool busy: false

    /*! 点「图片」/「文件」时，组件是否自己弹系统文件选择框。
        默认 \c true —— 不接线也能用。宿主想换成自己的选择器（比如带预览、
        带最近使用、或者远程目录浏览）时把它置 false，然后接
        \l imagePickRequested / \l filePickRequested 自己处理，
        选完再调 \l addImage / \l addFile / \l addUrlsAsAttachments。 */
    property bool useSystemDialogs: true

    // ════════════════════════════════════════════════════════════════
    //  3. 行为
    // ════════════════════════════════════════════════════════════════

    /*!
        Enter 的行为。`true`（默认）= Enter 发送、Shift+Enter 换行；
        `false` = Enter 换行、Ctrl+Enter 发送。两种模式下 Ctrl+Enter 都能发。
    */
    property bool sendOnEnter: true

    /*! 发出 \l sendRequested 之后是否自动清空草稿（默认不清，交给宿主决定） */
    property bool clearOnSend: false

    /*! 正文长度上限（字符数）。0 = 不限制；超限只是不能发送，不会截断用户输入 */
    property int maxLength: 2000

    /*! 附件数量上限，0 = 不限制 */
    property int maxAttachments: 9

    /*! 用户点了「发送」（或按了发送快捷键） */
    signal sendRequested(string text, var attachments)

    /*! 加进一个附件（\a index 是加入后的下标） */
    signal attachmentAdded(var attachment, int index)

    /*! 删掉一个附件 */
    signal attachmentRemoved(var attachment, int index)

    /*! 用户点了「图片」——宿主该打开文件选择框，选完调 \l addImage */
    signal imagePickRequested()

    /*! 用户点了「文件」——同上，选完调 \l addFile */
    signal filePickRequested()

    /*! 用户按了 Escape */
    signal cancelRequested()

    /*! 正文被编辑（每次内容变化都会发，宿主可用来做"正在输入"） */
    signal textEdited()

    /*!
        拖文件进输入框时发出，\a urls 是 `list<url>`。
        图片会按扩展名自动成为图片附件、其余成为文件附件（受 \l maxAttachments 限制）；
        这个信号只是给宿主"知道发生了什么"，**不需要**再自己调 \l addImage / \l addFile。
    */
    signal filesDropped(var urls)

    /*!
        用户在输入区按 Ctrl+V，而剪贴板里**没有文本**时发出——这通常意味着剪贴板里是位图。
        QML 没有读剪贴板图片的能力，需要宿主用 C++ 取图、存成文件，再回调 \l addImage：

        \qml
        ZChatInput {
            onPasteRequested: {
                var url = ZClipboard.saveImage()      // 宿主注入的 context property
                if (url.length > 0)
                    addImage(url, "clipboard.png")
            }
        }
        \endqml
    */
    signal pasteRequested()

    /*!
        某条限制被撞到。\a kind 是 `"length"`（超出 \l maxLength）或
        `"attachments"`（超出 \l maxAttachments），宿主可以据此弹提示。
    */
    signal limitReached(string kind)

    /*! 附件个数 */
    readonly property int attachmentCount: attachments ? attachments.length : 0

    /*! 其中图片个数 */
    readonly property int imageCount: countOfType("image")

    /*! 其中文件个数 */
    readonly property int fileCount: countOfType("file")

    /*! 正文 + 附件是否至少有一个非空 */
    readonly property bool hasContent: text.trim().length > 0 || attachmentCount > 0

    /*! 正文是否超过 \l maxLength */
    readonly property bool overLimit: maxLength > 0 && text.length > maxLength

    /*! 现在按「发送」会不会真的发出去 */
    readonly property bool canSend: hasContent && !readOnly && !busy && !overLimit

    /*! 输入区是否拿到了焦点（宿主可用来做"点击空白处聚焦"） */
    readonly property bool inputFocused: area.activeFocus

    /*! 标题条右侧的草稿摘要，例如 `"128 字 · 2 图 · 1 文件"` */
    readonly property string draftSummary: {
        var parts = [text.length + " 字"]
        if (imageCount > 0)
            parts.push(imageCount + " 图")
        if (fileCount > 0)
            parts.push(fileCount + " 文件")
        return parts.join(" · ")
    }

    /*! 计数文案，例如 `"128 / 2000"` */
    readonly property string counterText: maxLength > 0 ? (text.length + " / " + maxLength)
        : (text.length + " 字")

    /*! 内部：附件自增 id（只用来给委托一个稳定 key） */
    // 附件 id 的计数器。故意做成普通 JS 对象而不是 `property int`：
    // 工厂函数常被写在 `attachments: [ makeImageAttachment(...) ]` 这种**绑定**里，
    // 如果计数器是 QML 属性，绑定求值期间读写它会把自己标记成依赖，
    // 于是 Qt 报 "Binding loop detected for property attachments"。
    // 放在对象字段里，绑定时只读 `_idGen` 这个属性本身（值不变），写入不触发失效。
    property var _idGen: ({next: 1})

    /*! 内部：是否正把文件拖在输入框上 */
    property bool _dropActive: false

    /*! 内部：是否已经就"超长"报过警（用来做边沿触发，避免每敲一个字都发信号） */
    property bool _overLimitReported: false

    /*! 内部：是否已经就"超出输入框高度"报过警（同样是边沿触发） */
    property bool _overflowReported: false

    /*! 内部：正在把溢出的输入退回去，别再让这次回退再走一遍溢出判定 */
    property bool _revertingOverflow: false

    /*!
        内部：输入区（视口）的实际高度。

        只看文字的**固有**高度（\c contentHeight + padding），绝不去看 \c area.height ——
        否则 area.height 依赖视口、视口又依赖 contentHeight（contentHeight 反过来随宽高变），
        很容易绕成绑定循环。\l allowResize 关掉时它就是个常数 \l minTextHeight。
    */
    readonly property int _viewportHeight: allowResize
        ? Math.max(minTextHeight,
            Math.min(area.contentHeight + area.topPadding + area.bottomPadding,
                maxTextHeight))
        : minTextHeight

    /*! 内部：上一次的正文，用来做 diff 找出"这次粘贴进来的是哪一段" */
    property string _lastText: ""

    /*! 内部：正在把粘贴进来的 file:/// 文本改写成附件，别让改写本身再触发一次扫描 */
    property bool _suppressPasteScan: false

    /*! 内部：按扩展名认作图片的那些后缀（小写、不含点） */
    readonly property var _imageExtensions: [
        "png", "jpg", "jpeg", "jfif", "gif", "bmp", "webp", "svg",
        "ico", "tif", "tiff", "heic", "heif", "avif"
    ]

    implicitWidth: Theme.dp(560)
    implicitHeight: body.height

    // ════════════════════════════════════════════════════════════════
    //  4. 纯函数：附件工厂 / 归一化 / 换算 / 序列化
    //     全部不碰界面状态，可以直接在 qmltestrunner 里断言。
    // ════════════════════════════════════════════════════════════════

    /*! 造一个图片附件。 */
    function makeImageAttachment(url, name) {
        return normalizeAttachment({
            type: "image",
            url: url ? String(url) : "",
            name: name ? String(name) : (url ? fileNameOf(url) : "image")
        })
    }

    /*! 造一个文件附件。\a size 是字节数。 */
    function makeFileAttachment(url, name, size) {
        return normalizeAttachment({
            type: "file",
            url: url ? String(url) : "",
            name: name ? String(name) : (url ? fileNameOf(url) : "file"),
            size: size
        })
    }

    /*! 把一个可能残缺的附件对象补成合法形状（类型只认 image / file，其它按 file 处理）。 */
    function normalizeAttachment(att) {
        var src = att || {}
        var type = String(src.type || "").toLowerCase() === "image" ? "image" : "file"
        var url = src.url ? String(src.url) : ""
        var name = src.name ? String(src.name) : (url ? fileNameOf(url) : type)
        var size = Number(src.size)
        return {
            type: type,
            url: url,
            name: name,
            size: isFinite(size) && size > 0 ? Math.round(size) : 0,
            id: src.id ? String(src.id) : ("att-" + (composer._idGen.next++))
        }
    }

    /*! 从路径里取文件名（同时认 `/` 与 `\`）。 */
    function fileNameOf(url) {
        var s = url ? String(url) : ""
        var cut = Math.max(s.lastIndexOf("/"), s.lastIndexOf("\\"))
        return cut >= 0 ? s.substring(cut + 1) : s
    }

    /*! 取扩展名（小写、不含点）。没有扩展名返回空串。 */
    function extensionOf(name) {
        var s = name ? String(name) : ""
        var dot = s.lastIndexOf(".")
        if (dot <= 0 || dot === s.length - 1)
            return ""
        return s.substring(dot + 1).toLowerCase()
    }

    /*! 字节数 → 人读的大小。 */
    function formatBytes(bytes) {
        var n = Number(bytes)
        if (!isFinite(n) || n <= 0)
            return "0 B"
        if (n < 1024)
            return Math.round(n) + " B"
        if (n < 1024 * 1024)
            return (n / 1024).toFixed(1) + " KB"
        if (n < 1024 * 1024 * 1024)
            return (n / (1024 * 1024)).toFixed(1) + " MB"
        return (n / (1024 * 1024 * 1024)).toFixed(1) + " GB"
    }

    /*! 按扩展名猜一个 url 是不是图片（拖进来 / 粘进来的东西要靠它分流）。 */
    function isImageUrl(url) {
        var ext = extensionOf(fileNameOf(url ? String(url) : ""))
        return ext.length > 0 && _imageExtensions.indexOf(ext) >= 0
    }

    /*!
        把 \l FileDialog.selectedFiles（`list<url>`）转成普通字符串数组。
        单独抽出来是因为 QML 的列表属性在 JS 里是"类数组"对象，
        直接往 \l addUrlsAsAttachments 里塞，某些 Qt 版本会拿到带包装的对象。
    */
    function urlsOf(files) {
        var out = []
        if (!files)
            return out
        for (var i = 0; i < files.length; ++i) {
            var one = files[i]
            if (one === undefined || one === null)
                continue
            var s = String(one)
            if (s.length > 0)
                out.push(s)
        }
        return out
    }

    /*!
        取 \a after 相对 \a before 新插入的那一段：去掉共同前缀、共同后缀，中间就是这次改动的净增量。
        粘贴时用它认出"刚插进来的内容是什么"，纯函数，方便单测。
    */
    function insertedText(before, after) {
        var b = before === undefined || before === null ? "" : String(before)
        var a = after === undefined || after === null ? "" : String(after)
        var head = 0
        var headMax = Math.min(b.length, a.length)
        while (head < headMax && b.charAt(head) === a.charAt(head))
            ++head
        var tail = 0
        var tailMax = Math.min(b.length - head, a.length - head)
        while (tail < tailMax &&
            b.charAt(b.length - 1 - tail) === a.charAt(a.length - 1 - tail))
            ++tail
        return a.substring(head, a.length - tail)
    }

    /*!
        把一段文本按行拆开：**每一行都是 `file:///…`** 时返回这些 url，否则返回空数组。
        资源管理器里 Ctrl+C 复制文件，粘到 QML 的 TextEdit 里就是一行一个 file url，
        这样就能把它们转成附件，而不是把一串路径塞进正文。
    */
    function fileUrlsIn(block) {
        var s = block === undefined || block === null ? "" : String(block)
        if (s.length === 0)
            return []
        var lines = s.replace(/\r\n?/g, "\n").split("\n")
        var out = []
        for (var i = 0; i < lines.length; ++i) {
            var line = lines[i].replace(/^\s+/, "").replace(/\s+$/, "")
            if (line.length === 0)
                continue
            if (!/^file:\/\/\/\S+$/.test(line))
                return []
            out.push(line)
        }
        return out
    }

    /*! 附件数组里某种形态的个数。 */
    function countOfType(type) {
        var want = String(type || "")
        var n = 0
        for (var i = 0; i < attachments.length; ++i) {
            if (String(attachments[i].type) === want)
                ++n
        }
        return n
    }

    /*! 草稿序列化：`{ text, attachments }`，附件是浅拷贝（改返回值不影响界面）。 */
    function draftToJSON() {
        var list = []
        for (var i = 0; i < attachments.length; ++i) {
            var a = attachments[i]
            list.push({type: a.type, url: a.url, name: a.name, size: a.size, id: a.id})
        }
        return {text: text, attachments: list}
    }

    /*! 从 \l draftToJSON 的结构恢复草稿（字段缺失按空处理）。 */
    function draftFromJSON(draft) {
        var src = draft || {}
        text = src.text === undefined || src.text === null ? "" : String(src.text)
        var list = []
        var incoming = src.attachments
        if (incoming && incoming.length !== undefined) {
            for (var i = 0; i < incoming.length; ++i)
                list.push(normalizeAttachment(incoming[i]))
        }
        attachments = list
        revision++
    }

    /*! 当前按下这个修饰键组合时，回车是否应该"发送"。 */
    function isSendCombo(modifiers) {
        var ctrl = (modifiers & Qt.ControlModifier) !== 0
        var shift = (modifiers & Qt.ShiftModifier) !== 0
        if (ctrl)
            return true
        if (shift)
            return false
        return sendOnEnter
    }

    /*! 取一份附件快照（发给宿主的永远是新数组，宿主改它不会污染草稿）。 */
    function snapshotAttachments() {
        var list = []
        for (var i = 0; i < attachments.length; ++i) {
            var a = attachments[i]
            list.push({type: a.type, url: a.url, name: a.name, size: a.size, id: a.id})
        }
        return list
    }

    // ════════════════════════════════════════════════════════════════
    //  5. 操作：加 / 删附件、发送、清空
    // ════════════════════════════════════════════════════════════════

    /*!
        加一个附件，返回它加入后的下标；被拒绝（只读 / 超上限 / 无效）返回 -1。
        图片与文件走同一入口，形态由 \c att.type 决定。
    */
    function addAttachment(att) {
        if (readOnly)
            return -1
        if (!att || (att.url === undefined && att.name === undefined))
            return -1
        if (maxAttachments > 0 && attachmentCount >= maxAttachments) {
            limitReached("attachments")
            return -1
        }
        var normalized = normalizeAttachment(att)
        var list = attachments.slice()
        list.push(normalized)
        attachments = list
        revision++
        attachmentAdded(normalized, list.length - 1)
        return list.length - 1
    }

    /*! 加图片附件（\l addAttachment 的语法糖）。 */
    function addImage(url, name) {
        return addAttachment(makeImageAttachment(url, name))
    }

    /*! 加文件附件（\l addAttachment 的语法糖）。 */
    function addFile(url, name, size) {
        return addAttachment(makeFileAttachment(url, name, size))
    }

    /*!
        把一批 url（拖进来、粘进来）加成附件：图片后缀走 \l addImage，其余走 \l addFile。
        遵守 \l maxAttachments，越界的部分会被 \l limitReached 挡掉。返回真正加进去的个数。
    */
    function addUrlsAsAttachments(urls) {
        if (readOnly || !urls)
            return 0
        var count = urls.length
        if (count === undefined)
            return 0
        var added = 0
        for (var i = 0; i < count; ++i) {
            var u = urls[i] === undefined || urls[i] === null ? "" : String(urls[i])
            if (u.length === 0)
                continue
            var name = fileNameOf(u)
            var idx = isImageUrl(u) ? addImage(u, name) : addFile(u, name, 0)
            if (idx >= 0)
                ++added
        }
        return added
    }

    /*! 删掉第 \a index 个附件，返回是否删掉了。 */
    function removeAttachment(index) {
        if (readOnly)
            return false
        if (!(index >= 0 && index < attachments.length))
            return false
        var list = attachments.slice()
        var removed = list[index]
        list.splice(index, 1)
        attachments = list
        revision++
        attachmentRemoved(removed, index)
        return true
    }

    /*! 清空全部附件。 */
    function clearAttachments() {
        if (attachments.length === 0)
            return
        attachments = []
        revision++
    }

    /*! 清空草稿（正文 + 附件）。 */
    function clear() {
        area.text = ""
        clearAttachments()
        _overLimitReported = false
        _overflowReported = false
    }

    /*!
        尝试发送。返回是否真的发出去了：
        没有内容 / 只读 / 忙碌 / 超长 时返回 `false`，并不发信号。
    */
    function submit() {
        if (!hasContent)
            return false
        if (readOnly || busy)
            return false
        if (overLimit) {
            limitReached("length")
            return false
        }
        sendRequested(area.text, snapshotAttachments())
        if (clearOnSend)
            clear()
        return true
    }

    /*! 把焦点放进输入区。 */
    function focusInput() {
        area.forceActiveFocus()
    }

    /*! 输入区超过 maxTextHeight 后靠内部 Flickable 滚动，把光标拉回可视区。

        不这么做的话：TextArea 高度封顶、只 clip 不滚，光标走到第 9 行以后就停在
        可视区外面，用户看起来就是"文字和光标跑到输入框下面消失"。
    */
    function _keepCaretVisible() {
        if (typeof textFlick === "undefined" || textFlick === null
            || typeof area === "undefined" || area === null)
            return
        // 内容没超：永远回到顶部，别留着上一次的滚动位置
        if (textFlick.contentHeight <= textFlick.height) {
            if (textFlick.contentY !== 0)
                textFlick.contentY = 0
            return
        }
        var r = area.cursorRectangle
        var viewTop = textFlick.contentY
        var viewBottom = viewTop + textFlick.height
        if (r.y < viewTop) {
            textFlick.contentY = Math.max(0, r.y - 2)
        } else if (r.y + r.height > viewBottom) {
            textFlick.contentY = Math.min(textFlick.contentHeight - textFlick.height,
                r.y + r.height - textFlick.height + 2)
        }
    }

    /*! 内部：把输入区滚到头之后剩下的那段滚轮距离，转给最近的可滚动祖先。

        只在 \l wheelChainsToAncestor 打开时才会被调用（默认关，到头就停住）。
        为什么要自己转：`WheelHandler` 一旦挂在组件里，滚轮事件就由它接管、**不再冒泡**，
        想"继续滚外层"就只能手动把剩余距离递过去
        （`Flickable` / `ListView` / \c ScrollView 的 contentItem 都只看 contentY）。
    */
    function _chainWheelToAncestor(dy) {
        // parent 的静态类型只是 Item，直接读 contentY 会被静态检查拦下，
        // 所以先 instanceof 判断、再 as Flickable 收窄
        var p = parent
        while (p) {
            if (p instanceof Flickable) {
                var f = p
                as
                Flickable
                if (f.contentHeight > f.height + 0.5) {
                    var maxY = f.contentHeight - f.height
                    f.contentY = Math.max(0, Math.min(maxY, f.contentY - dy))
                    return true
                }
            }
            p = p.parent
        }
        return false
    }

    /*! 内部：把这次会让内容溢出视口的输入整段退回（\l allowOverflow 为 false 时用）。

        为什么整段退回、而不是截成"刚好放得下"：截断需要拿候选文本反复量 \c contentHeight，
        而 TextEdit 的排版是异步的，量到的值不可靠；退回上一次的正文则是确定性的，
        连按退格 / 连续打字都不会出现"字越打越少"的半截状态。
    */
    function _rejectOverflow() {
        var previous = _lastText
        if (area.text === previous)
            return
        var keepCursor = Math.min(area.cursorPosition, previous.length)
        _revertingOverflow = true
        area.text = previous
        area.cursorPosition = keepCursor
        if (!_overflowReported) {
            _overflowReported = true
            limitReached("height")
        }
    }

    // ════════════════════════════════════════════════════════════════
    //  6. 界面
    // ════════════════════════════════════════════════════════════════

    ZCutRect {
        id: bg
        anchors.fill: parent
        color: Theme.surface
        strokeColor: composer._dropActive ? Theme.accent
            : (area.activeFocus ? Theme.line : Theme.lineSoft)
        strokeWidth: Theme.border
        cutTopRight: composer.framed ? Theme.cutMd : 0
        cutBottomLeft: composer.framed ? Theme.cutMd : 0
    }

    Column {
        id: body
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top

        // ── 标题条 ────────────────────────────────────────────────
        ZCutRect {
            id: headerBar
            objectName: "chatHeader"
            width: parent.width
            height: composer.showHeader ? 30 : 0
            visible: composer.showHeader
            // 用 inkBlock 而不是 ink：ink 是**前景色**，暗色模式下会翻成近白，
            // 这条标题条就成了白底白字（文字用的是 textOnSub）。
            // 库里其它"黑色块"（ZPanel / ZSectionTitle / ZTable / ZDialog）都用 inkBlock。
            color: Theme.inkBlock
            cutTopRight: composer.framed ? Theme.cutMd : 0

            Text {
                id: headerLabel
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingLg
                anchors.verticalCenter: parent.verticalCenter
                text: composer.headerText
                font: Theme.fontMonoSm
                color: Theme.textOnSub
            }

            Text {
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingLg
                anchors.verticalCenter: parent.verticalCenter
                text: composer.draftSummary.toUpperCase()
                font: Theme.fontMonoSm
                color: composer.hasContent ? Theme.accent : Theme.muted
            }
        }

        // ── 附件预览区 ────────────────────────────────────────────
        // 高度**与附件数量无关**：永远只占一行（thumbSize + 上下留白），排不下就横向滚。
        // 之前用 Flow 自动换行，第 4、5 个附件就会折到第二行，整个组件被顶高，
        // 把下面的正文、工具栏一起推下去 —— 用户看到的就是"一插图片窗口就变高"。
        Item {
            id: attachZone
            objectName: "chatAttachZone"
            width: parent.width
            // 没有附件时附件区高度为 0 —— 提示行默认是空的（attachHintText 默认 ""），
            // 宿主自己填了提示、且输入区还空着，才给它 26px。
            height: composer.attachmentCount > 0 ? composer.thumbSize + Theme.spacingSm * 2
                : (composer.attachHintText.length > 0 && composer.text.length === 0 ? 26 : 0)
            visible: height > 0

            Flickable {
                id: attachFlick
                objectName: "chatAttachScroller"
                x: Theme.spacingMd
                y: Theme.spacingSm
                width: parent.width - Theme.spacingMd * 2
                height: composer.thumbSize
                contentWidth: attachRow.width
                contentHeight: height
                clip: true
                visible: composer.attachmentCount > 0
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds

                // 竖向滚轮在附件条上映射成横向滚动（触控板的横向增量照用）
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedModifiers: Qt.NoModifier

                    onWheel: function (event) {
                        var maxX = Math.max(0, attachFlick.contentWidth - attachFlick.width)
                        if (maxX <= 0) {
                            event.accepted = false
                            return
                        }
                        var dx = event.pixelDelta.x !== 0 ? event.pixelDelta.x
                            : event.pixelDelta.y !== 0 ? event.pixelDelta.y
                                : (event.angleDelta.x !== 0 ? event.angleDelta.x : event.angleDelta.y)
                                / 120 * composer.wheelStepSize
                        attachFlick.contentX = Math.max(0, Math.min(maxX, attachFlick.contentX - dx))
                        event.accepted = true
                    }
                }

                Row {
                    id: attachRow
                    width: childrenRect.width
                    height: composer.thumbSize
                    spacing: Theme.spacingSm

                    Repeater {
                        model: composer.attachments

                        delegate: Loader {
                            id: attachLoader

                            // 文件卡比缩略图矮，Row 里要自己垂直居中
                            y: Math.round((composer.thumbSize - height) / 2)

                            required property int index
                            required property var modelData

                            // 委托里的两个 Component 声明在文件级作用域，看不到 attachLoader 这个 id，
                            // 但它们靠上下文直接读 modelData / index —— 这两个是 Repeater 委托的上下文属性，
                            // sourceComponent 造出来的项会继承它。
                            sourceComponent: modelData && modelData.type === "image" ? imageThumb : fileCard
                        }
                    }
                }
            }

            Text {
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                visible: composer.attachmentCount === 0
                    && composer.text.length === 0
                    && composer.attachHintText.length > 0
                text: composer.attachHintText
                font: Theme.fontMonoSm
                color: Theme.muted
            }
        }

        // ── 输入区 ────────────────────────────────────────────────
        Item {
            id: textWrap
            width: parent.width
            height: textFlick.height

            Text {
                id: placeholderLabel
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: Theme.spacingMd
                anchors.rightMargin: Theme.spacingMd
                anchors.topMargin: Theme.spacingSm
                text: composer.placeholderText
                font: Theme.fontBody
                color: Theme.muted
                visible: area.text.length === 0
                    && !composer.readOnly
                    && composer.placeholderText.length > 0
                elide: Text.ElideRight
            }

            // 输入区高度 = 视口高度（见 composer._viewportHeight）：
            // 关掉 allowResize 就是个定高框，超出部分靠这个 Flickable 内部滚动。
            // 不能只靠 TextArea 自己的 clip：那样第 9 行以后会被裁掉且**不会滚动**，
            // 光标停在可视区外，用户看到的就是"文字和光标跑到框下面消失"。
            Flickable {
                id: textFlick
                objectName: "chatTextScroller"
                width: parent.width
                height: composer._viewportHeight
                contentWidth: width
                contentHeight: area.height
                clip: true
                // 拖拽留给文字选择，滚动靠滚轮 / 滚动条 / 光标跟随；
                // 否则一拖鼠标就在"划选文字"和"滚动"之间打架
                interactive: false
                boundsBehavior: Flickable.StopAtBounds

                // 滚轮必须自己接管：Flickable 在 interactive=false 时**不处理滚轮**
                // （Qt 6 的 QQuickFlickable::wheelEvent 一进门就 return），
                // 事件于是冒泡给外层页面 —— 用户看到的就是"在输入框里滚，页面在动"。
                // 所以这里滚内部视口，并且默认**滚到边界就停住**，不带动外层。
                WheelHandler {
                    id: textWheel
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    acceptedModifiers: Qt.NoModifier

                    onWheel: function (event) {
                        // 触控板给的是像素增量，鼠标给的是 1/8 度的角度增量（一格 120）
                        var dy = event.pixelDelta.y !== 0
                            ? event.pixelDelta.y
                            : event.angleDelta.y / 120 * composer.wheelStepSize
                        var maxY = Math.max(0, textFlick.contentHeight - textFlick.height)

                        // 内容放得下：输入区根本没有可滚区域，整个事件交给外层页面。
                        // （这不算"到边界"，否则一行字的输入框会变成页面滚动的死区。）
                        if (maxY <= 0) {
                            composer._chainWheelToAncestor(dy)
                            event.accepted = true
                            return
                        }

                        var next = Math.max(0, Math.min(maxY, textFlick.contentY - dy))
                        var used = textFlick.contentY - next
                        textFlick.contentY = next
                        // 默认到头就停住：剩下的距离直接丢掉，外层页面一动不动。
                        // 想要浏览器那种"滚到头继续滚页面"，把 wheelChainsToAncestor 打开。
                        var left = dy - used
                        if (composer.wheelChainsToAncestor && Math.abs(left) > 0.5)
                            composer._chainWheelToAncestor(left)
                        event.accepted = true
                    }
                }

                // 内容真的超出视口时才挂滚动条：allowOverflow 关掉时内容永远不超出，
                // 所以那条分支里它恒为 AlwaysOff。
                ScrollBar.vertical: ZScrollBar {
                    id: textScrollBar
                    objectName: "chatTextScrollBar"
                    policy: (composer.allowOverflow && textFlick.contentHeight > textFlick.height + 0.5)
                        ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
                }

                TextArea {
                    id: area
                    objectName: "chatTextArea"

                    // 滚动条占的是右侧 12px 的内边距，正好压在 rightPadding 的空当里，
                    // 所以它出现 / 消失不会让文字重新折行（不会和视口高度互相抖）
                    width: textFlick.width
                    // 允许超出时：内容高过视口就把自身撑高，交给外层 Flickable 滚；
                    // 不允许超出时内容不会超过视口（溢出的输入已被退回），高度就锁在视口高
                    height: composer.allowOverflow
                        ? Math.max(textFlick.height, contentHeight + topPadding + bottomPadding)
                        : textFlick.height

                    leftPadding: Theme.spacingMd
                    rightPadding: Theme.spacingMd
                    topPadding: Theme.spacingSm
                    bottomPadding: Theme.spacingSm

                    readOnly: composer.readOnly
                    selectByMouse: true
                    wrapMode: TextEdit.Wrap
                    clip: true
                    background: null

                    color: Theme.ink
                    selectionColor: Theme.accent
                    selectedTextColor: Theme.textOnAccent
                    font: Theme.fontBody
                    textFormat: TextEdit.PlainText

                    cursorDelegate: Rectangle {
                        width: Theme.border
                        color: Theme.accent
                        visible: area.cursorVisible
                    }

                    // 光标跑了就把它带回可视区（打字、回车、粘贴、方向键都会走到这里）
                    onCursorRectangleChanged: composer._keepCaretVisible()

                    Keys.onPressed: function (event) {
                        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                            if (composer.isSendCombo(event.modifiers)) {
                                event.accepted = true
                                composer.submit()
                            }
                            return
                        }
                        if (event.key === Qt.Key_Escape) {
                            event.accepted = true
                            composer.cancelRequested()
                        }
                        // Ctrl+V 而剪贴板里没有文本 = 多半是位图。QML 读不到位图，交给宿主。
                        if (event.key === Qt.Key_V
                            && (event.modifiers & Qt.ControlModifier) !== 0
                            && (event.modifiers & Qt.ShiftModifier) === 0
                            && !composer.readOnly
                            && !area.canPaste) {
                            event.accepted = true
                            composer.pasteRequested()
                        }
                    }

                    onTextChanged: {
                        // 上一次是"溢出退回"造成的改动：收工，别再做溢出判定 / 粘贴扫描，
                        // 否则退回自己又会触发一次退回。
                        if (composer._revertingOverflow) {
                            composer._revertingOverflow = false
                            return
                        }

                        // allowOverflow 关掉时，输入区高度就是硬上限：
                        // 这次输入如果让内容高过视口，整段退回（见 _rejectOverflow）。
                        if (!composer.allowOverflow
                            && contentHeight + topPadding + bottomPadding
                            > composer._viewportHeight + 0.5) {
                            composer._rejectOverflow()
                            return
                        }

                        // 资源管理器里 Ctrl+C 复制文件，粘进来是一行一个 file:///…。
                        // 拦下来转成附件，别把一串路径塞进正文。
                        // 只在输入区有焦点时扫：程序化地设 text（恢复草稿、演示页的初始文案）
                        // 不该被当成粘贴处理。
                        if (composer._suppressPasteScan) {
                            composer._suppressPasteScan = false
                        } else if (area.activeFocus) {
                            var inserted = composer.insertedText(composer._lastText, area.text)
                            var pastedUrls = composer.fileUrlsIn(inserted)
                            if (pastedUrls.length > 0) {
                                // 先把正文还原回粘贴前（这次赋值会再进一次本函数，
                                // 被 _suppressPasteScan 挡掉），再把 url 变成附件。
                                composer._suppressPasteScan = true
                                area.text = composer._lastText
                                composer.addUrlsAsAttachments(pastedUrls)
                                return
                            }
                        }
                        composer._lastText = area.text

                        // 回到能放下的高度了，下次再溢出时要重新报一次
                        if (contentHeight + topPadding + bottomPadding
                            <= composer._viewportHeight + 0.5)
                            composer._overflowReported = false

                        // 只越过上限的那一刻报一次，否则每敲一个字都会甩一条信号出去。
                        if (composer.overLimit && !composer._overLimitReported) {
                            composer._overLimitReported = true
                            composer.limitReached("length")
                        } else if (!composer.overLimit) {
                            composer._overLimitReported = false
                        }
                        composer.textEdited()
                    }
                }
            }
        }

        // ── 工具栏 ────────────────────────────────────────────────
        Item {
            id: toolbar
            width: parent.width
            height: composer.showToolbar ? 42 : 0
            visible: composer.showToolbar

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: Theme.border
                color: Theme.lineSoft
            }

            Row {
                id: chipBar
                anchors.left: parent.left
                anchors.leftMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.spacingXs

                ToolChip {
                    caption: "图片"
                    visible: composer.allowImage
                    enabled: !composer.readOnly
                    onClicked: composer.openImagePicker()
                }

                ToolChip {
                    caption: "文件"
                    visible: composer.allowFile
                    enabled: !composer.readOnly
                    onClicked: composer.openFilePicker()
                }

                Item {
                    width: Theme.spacingMd
                    height: 1
                }

                ToolChip {
                    caption: "清空"
                    enabled: !composer.readOnly && composer.hasContent
                    onClicked: composer.clear()
                }
            }

            // 右侧从内到外：发送按钮 ← 字数 ← 快捷键提示。
            // 三者都贴 sendButton.left 会叠在一起（提示和字数互相压字）。
            Text {
                id: counterLabel
                anchors.right: sendButton.left
                anchors.rightMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                text: composer.counterText
                font: Theme.fontMonoSm
                color: composer.overLimit ? Theme.danger : Theme.muted
            }

            Text {
                id: hintLabel
                anchors.right: counterLabel.left
                anchors.rightMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, parent.width - chipBar.width - sendButton.width
                    - counterLabel.width - Theme.spacingMd * 5)
                horizontalAlignment: Text.AlignRight
                elide: Text.ElideRight
                text: composer.sendOnEnter ? "ENTER 发送 · SHIFT+ENTER 换行"
                    : "ENTER 换行 · CTRL+ENTER 发送"
                font: Theme.fontMonoSm
                color: Theme.muted
            }

            ZButton {
                id: sendButton
                anchors.right: parent.right
                anchors.rightMargin: Theme.spacingMd
                anchors.verticalCenter: parent.verticalCenter
                height: Theme.controlHeightSm
                text: composer.busy ? "发送中" : composer.sendText
                variant: Theme.Variant.Primary
                // mono 在 ZButton 里是"右侧等宽小字"（快捷键提示），不是布尔开关
                mono: composer.sendOnEnter ? "ENTER" : "CTRL+↵"
                enabled: composer.canSend
                onClicked: composer.submit()
            }
        }
    }

    // ── 图片缩略图委托 ────────────────────────────────────────────
    Component {
        id: imageThumb

        Item {
            id: thumb
            width: composer.thumbSize
            height: composer.thumbSize

            readonly property var att: modelData

            ZCutRect {
                anchors.fill: parent
                color: Theme.surfaceMuted
                strokeColor: Theme.lineSoft
                strokeWidth: Theme.border
                cutTopRight: Theme.cutSm
                cutBottomLeft: Theme.cutSm
            }

            Image {
                id: thumbImage
                anchors.fill: parent
                anchors.margins: 2
                source: thumb.att && thumb.att.url ? thumb.att.url : ""
                sourceSize.width: 160
                sourceSize.height: 160
                fillMode: Image.PreserveAspectCrop
                clip: true
                asynchronous: true
                visible: status === Image.Ready
            }

            // 没图 / 破图时的占位：斜线纹理 + IMG 徽标
            Item {
                anchors.fill: parent
                anchors.margins: 2
                visible: !thumbImage.visible

                ZHatchPattern {
                    anchors.fill: parent
                    lineColor: Theme.hatchStrong
                    spacing: Theme.hatchStep
                }

                Text {
                    anchors.centerIn: parent
                    text: "IMG"
                    font: Theme.fontMonoSm
                    color: Theme.muted
                }
            }

            // 文件名压条：内缩到描边里面，并带上和缩略图一样的左下切角，
            // 否则会盖住底板描边、把左下角的切角顶成直角。
            ZCutRect {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: 2
                anchors.rightMargin: 2
                anchors.bottomMargin: 2
                height: 15
                objectName: "chatThumbName"
                // 同标题条：黑底压条要用 inkBlock，ink 在暗色下是近白
                color: Theme.inkBlock
                opacity: 0.82
                cutTopRight: Theme.cutSm
                cutBottomLeft: Theme.cutSm

                Text {
                    anchors.left: parent.left
                    anchors.leftMargin: 3
                    anchors.right: parent.right
                    anchors.rightMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    text: thumb.att ? thumb.att.name : ""
                    font: Qt.font({family: Theme.familyMono, pixelSize: 10})
                    color: Theme.textOnSub
                    elide: Text.ElideMiddle
                }
            }

            RemoveBadge {
                anchors.right: parent.right
                anchors.top: parent.top
                onClicked: composer.removeAttachment(index)
            }

            // 缩略图只有 64 见方，文件名一定会被省略号吃掉，悬停给全名
            ZToolTip {
                text: thumb.att ? thumb.att.name : ""
            }
        }
    }

    // ── 文件卡片委托 ──────────────────────────────────────────────
    Component {
        id: fileCard

        ZCutRect {
            id: card

            readonly property var att: modelData
            readonly property string ext: composer.extensionOf(att ? att.name : "")

            width: composer.fileCardWidth
            height: composer.fileCardHeight
            color: Theme.surfaceMuted
            strokeColor: Theme.lineSoft
            strokeWidth: Theme.border
            cutTopRight: Theme.cutSm
            cutBottomLeft: Theme.cutSm

            ZCutRect {
                id: extBadge
                objectName: "chatExtBadge"
                width: 40
                // 描边有宽度，徽标要收在描边内侧，否则黑块会盖过卡片边框
                y: Theme.border
                height: parent.height - Theme.border * 2
                // 扩展名徽标同理：黑块要 inkBlock，否则暗色下变成白块压白字
                color: Theme.inkBlock
                // 左下角也要切，不然黑块会把卡片左下角的切角顶出去
                cutTopRight: Theme.cutSm
                cutBottomLeft: Theme.cutSm

                Text {
                    anchors.centerIn: parent
                    text: card.ext.length > 0 ? card.ext.toUpperCase() : "FILE"
                    font: Qt.font({family: Theme.familyMono, pixelSize: 11, bold: true})
                    color: Theme.textOnSub
                    elide: Text.ElideRight
                    width: parent.width - 6
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Text {
                id: fileNameLabel
                anchors.left: extBadge.right
                anchors.leftMargin: Theme.spacingSm
                anchors.right: removeBadge.left
                anchors.rightMargin: 4
                anchors.top: parent.top
                anchors.topMargin: Theme.spacingSm
                text: card.att ? card.att.name : ""
                font: Theme.fontStrong
                color: Theme.ink
                elide: Text.ElideMiddle
            }

            Text {
                anchors.left: extBadge.right
                anchors.leftMargin: Theme.spacingSm
                anchors.top: fileNameLabel.bottom
                anchors.topMargin: 2
                // 拖进来 / 粘进来的文件拿不到字节数（QML 没有 stat 能力），
                // 这时候只写 ATTACHMENT，别显示 "0 B" 骗人。
                text: (card.att && card.att.size > 0
                    ? composer.formatBytes(card.att.size) + "  ·  " : "") + "ATTACHMENT"
                font: Theme.fontMonoSm
                color: Theme.muted
            }

            RemoveBadge {
                id: removeBadge
                anchors.right: parent.right
                anchors.top: parent.top
                onClicked: composer.removeAttachment(index)
            }
        }
    }

    // ── 小部件：工具栏芯片 ────────────────────────────────────────
    component ToolChip: Button {
        id: chip

        property string caption

        implicitHeight: 26
        // 窄内边距：一行要放下三颗芯片 + 提示 + 计数 + 发送按钮
        implicitWidth: Math.max(28, chipLabel.implicitWidth + Theme.spacingSm * 2)
        padding: 0
        focusPolicy: Qt.NoFocus
        hoverEnabled: true

        background: ZCutRect {
            color: chip.down ? Theme.accent
                : chip.hovered ? Theme.surfaceHover
                    : "transparent"
            strokeColor: chip.hovered || chip.down ? Theme.line : Theme.lineSoft
            strokeWidth: Theme.border
            cutTopRight: Theme.cutSm
            opacity: chip.enabled ? 1.0 : 0.45
        }

        contentItem: Text {
            id: chipLabel
            text: chip.caption
            font: Qt.font({
                family: Theme.family,
                pixelSize: 12,
                bold: chip.hovered && chip.enabled
            })
            color: chip.down ? Theme.textOnAccent : (chip.enabled ? Theme.ink : Theme.muted)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    // ── 小部件：附件上的删除角标 ──────────────────────────────────
    component RemoveBadge: Item {
        id: badge

        signal clicked()

        width: 18
        height: 18

        ZCutRect {
            anchors.fill: parent
            objectName: "chatRemoveBadge"
            // 黑色角标：同样必须是 inkBlock，用 ink 暗色下会变成浅灰块配白 ×
            color: Theme.inkBlock
            cutBottomLeft: Theme.cutSm
            opacity: badgeHover.hovered ? 1.0 : 0.72
        }

        Text {
            anchors.centerIn: parent
            text: "×"
            font: Qt.font({family: Theme.familyMono, pixelSize: 12, bold: true})
            color: Theme.textOnSub
        }

        HoverHandler {
            id: badgeHover
        }

        TapHandler {
            acceptedButtons: Qt.LeftButton
            onTapped: badge.clicked()
        }
    }

    /*!
        打开「选图片」：发 \l imagePickRequested，并在 \l useSystemDialogs 为真时弹出
        系统文件选择框。工具栏的「图片」按钮走的就是这里；宿主想从菜单、快捷键
        触发同一个动作，直接调它即可。
    */
    function openImagePicker() {
        imagePickRequested()
        if (useSystemDialogs)
            imageDialog.open()
    }

    /*! 打开「选文件」：发 \l filePickRequested，并（在 \l useSystemDialogs 为真时）弹选择框。 */
    function openFilePicker() {
        filePickRequested()
        if (useSystemDialogs)
            fileDialog.open()
    }

    // ── 系统文件选择框 ────────────────────────────────────────────
    // Windows / macOS / Linux 上 Qt 走各自的原生对话框；没有原生实现的平台退回
    // Qt Quick 自绘的那套。选完直接按扩展名分流加进附件。
    FileDialog {
        id: imageDialog
        title: "选择图片"
        fileMode: FileDialog.OpenFiles
        nameFilters: [
            "图片 (*.png *.jpg *.jpeg *.jfif *.gif *.bmp *.webp *.svg *.ico *.tif *.tiff *.heic *.avif)",
            "所有文件 (*)"
        ]
        onAccepted: composer.addUrlsAsAttachments(composer.urlsOf(imageDialog.selectedFiles))
    }

    FileDialog {
        id: fileDialog
        title: "选择文件"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["所有文件 (*)"]
        onAccepted: composer.addUrlsAsAttachments(composer.urlsOf(fileDialog.selectedFiles))
    }

    // ── 拖放 ──────────────────────────────────────────────────────
    // 注意：TextArea 自己也会接拖放（并把文件路径当文本插进来），
    // 所以 DropArea 必须声明在它之后（叠在上面）才能真正拿到 drop 事件。
    DropArea {
        id: dropZone
        anchors.fill: parent
        onEntered: composer._dropActive = true
        onExited: composer._dropActive = false
        onDropped: function (drop) {
            composer._dropActive = false
            if (!drop.hasUrls)
                return
            composer.filesDropped(drop.urls)
            composer.addUrlsAsAttachments(drop.urls)
        }
    }

    ZCutRect {
        anchors.fill: parent
        anchors.margins: Theme.border
        visible: composer._dropActive
        color: Theme.alpha(Theme.accent, 0.16)
        strokeColor: Theme.accent
        strokeWidth: Theme.border
        cutTopRight: composer.framed ? Theme.cutMd : 0
        cutBottomLeft: composer.framed ? Theme.cutMd : 0

        Text {
            anchors.centerIn: parent
            text: "松开添加附件 / DROP TO ATTACH"
            font: Theme.fontMonoSm
            color: Theme.ink
        }
    }
}
