import QtQuick
import ZmdUI 1.0

import MyChat.Core 1.0
import "../../components"

/*!
    消息列表（会话右栏中间那一块）：气泡、时间标签、滚动条、翻页与贴底跟随。

    **为什么是 Flickable + Column 而不是 ListView**：ListView 复用 delegate，视口外
    的条目高度是按平均行高"估"出来的。本项目的消息气泡高度差得离谱（一行十来个字
    到整屏 430 字），估算值实测在 4446~21765 之间乱跳：拉到底再往上跳时 contentHeight
    会炸、originY 变负、所有 delegate 被回收 —— 屏幕一片空白且不会自愈；滚动条按
    contentHeight 映射，滑块拖到一半内容就"滚完了"。Column 把每条消息都摆出来，
    高度是实数：contentHeight 精确、contentY 放哪就是哪、滚动条比例天然正确。
    代价是全部 delegate 常驻内存 —— 每页 20 条地拉，几百条轻量气泡完全够用。

    对外只暴露"动作 + 只读状态"，模型同步由 \l syncMessageModel() 由 ChatPane 驱动。
*/

Item {
    id: view

    // ══════════════════════════════════════════════════════════════
    //  对外接口（ChatPane 写入 / 调用）
    // ══════════════════════════════════════════════════════════════

    /// 选择模式：整行可点，点一下切换选中。选中集合由 ChatPane 持有
    property bool selecting: false
    /// "id -> true" 的选中集合（整对象替换才触发重算，见 ChatPane.toggleSelection）
    property var selectedIds: ({})
    /*!
        右键菜单所在的坐标系。菜单画在 ChatPane 上，所以由它把 `pane` 传进来；
        不传就退回本组件（自检 / 单独使用时坐标只在本组件内有效）。
    */
    property Item menuSpace: null

    /// 消息条数（Flickable 没有 count，这里给出和 ListView 一样的读法）
    readonly property int count: messageModel.count

    /*!
        贴底跟随开关（转发给内部 Flickable）。

        ChatPane 在"自己发消息"前会先把它打开 —— 少了这个转发，
        `messageList.followTail = true` 会抛
        「Cannot assign to non-existent property」并中断整个 doSendMessage，
        表现就是**输入框有字但消息发不出去**。
    */
    property alias followTail: messageView.followTail

    /// 视口位置（自动化滚动用；同样转发给内部 Flickable）
    property alias contentY: messageView.contentY

    /// 右键某条消息（坐标已 map 到 menuSpace）
    signal contextMenuRequested(string msgId, string text, real x, real y)
    /// 选择模式下点了某条（ChatPane 负责维护选中集合）
    signal toggleRequested(string msgId)

    /*!
        群成员列表（GroupController.memberList）。

        存成属性而不是在函数里直接调 `GroupController.memberList` ——
        QML 对"函数调用里的属性访问"不建立依赖，群昵称到了界面不会刷新
        （和 ChatPane 里 PresenceController 那个坑是同一类）。
    */
    property var groupMembers: GroupController.memberList

    /*!
        气泡上该显示谁的名字。

        · 单聊：整列都是同一个人 → 用会话名，和以前完全一致；
        · 群聊：每条消息的发送者不同 → 优先**群昵称**（服务端 1042 给的
          `nick_g`），其次好友备注 / 昵称（ChatController 算好的 senderName），
          最后退回 uid。
    */
    function senderLabel(senderUid, fallback) {
        if (!ChatController.activeIsGroup)
            return ChatController.activePeerName

        for (let i = 0; i < groupMembers.length; ++i) {
            if (String(groupMembers[i].uid) !== senderUid)
                continue
            const nick = groupMembers[i].nickG
            if (nick !== undefined && String(nick).length > 0)
                return String(nick)
            break
        }
        return fallback.length > 0 ? fallback : senderUid
    }

    /// 把视口摁到最新一条（换会话 / 自己发消息 / 点「新消息 ↓」）
    function pinToBottom() {
        messageView.pinToBottom()
    }

    function atBottom() {
        return messageView.atBottom()
    }

    /// 翻页后把视口钉回原来那条消息（ChatPane 在头部新增时调用）
    function restoreAnchor(added, chBefore) {
        messageView.restoreAnchor(added, chBefore)
    }

    /// 自动化 / 日志用的只读量（Flickable 的 contentHeight 在 Item 上看不到）
    readonly property real contentHeightAll: messageView.contentHeight

    // ══════════════════════════════════════════════════════════════
    //  消息模型（增量同步，见 syncMessageModel）
    // ══════════════════════════════════════════════════════════════

    ListModel {
        id: messageModel
    }

Flickable {
    id: messageView
    anchors.fill: parent
    clip: true
    contentWidth: width
    contentHeight: msgColumn.height
    boundsBehavior: Flickable.StopAtBounds

    /// 消息条数（Flickable 没有 count，这里给出和 ListView 一样的读法）
    readonly property int count: messageModel.count

    /*
     * 贴底跟随（聊天软件的习惯）：在底部时新消息跟着滚，
     * 人往上翻历史时**不能**把人拽回来。
     */
    property bool followTail: true

    /// 用户正在拖滚动条：这期间的位置变化不算"用户上滚"
    property bool _barScrolling: false

    /// 正在把自己摁到底：这期间的 contentY 变化不算"用户离开底部"
    property bool _snapping: false

    /// snapToBottom 补帧次数（新气泡的高度可能晚一帧才算进 Column）
    property int _snapTries: 0

    /// 上一次记下的 contentY：用来分辨"用户在滚"和"布局自己动了零点几像素"
    property real _lastContentY: 0

    /// 往上翻页的闸门：一页没落定之前不再拉下一页
    property bool _paging: false

    /*
     * 翻页锚点：视口顶端那条消息是第几条、它离视口顶端多远。
     * 更早的一页插进模型头部以后，把同一条消息放回同样的位置 ——
     * 屏幕上就是"内容没动，上面接了一页"，而不是整屏往下跳。
     */
    property int _anchorIndex: -1
    property real _anchorOffset: 0

    /// 摆视口用的中间量
    property int _restoreIndex: -1
    property real _restoreOffset: 0
    property real _restoreCh: 0
    property int _restoreTries: 0

    /// 离底部还有多少像素（Column 的高度是实数，这里是精确值）
    function distanceToBottom() {
        return contentHeight - height - contentY
    }

    function atBottom() {
        return distanceToBottom() <= 8
    }

    /// 真正能滚到的最远处
    function maxContentY() {
        return Math.max(0, contentHeight - height)
    }

    /// 滚停之后把视口收进真实范围（惯性有时会冲过头）
    function settleScroll() {
        if (contentY > maxContentY())
            contentY = maxContentY()
    }

    /// 把"在不在底部"报给控制器 —— 只有它决定收到的消息算不算已读
    function syncReadState() {
        ChatController.chatAtBottom = atBottom()
    }

    /// 用户开始拖滚动条：交出方向盘，别再跟尾
    function beginBarScroll() {
        _barScrolling = true
        followTail = false
    }

    /// 用户松手：接回方向盘，按真实位置重新判定
    function endBarScroll() {
        _barScrolling = false
        syncReadState()
        followTail = atBottom()
        settleScroll()
    }

    /*
     * 往上翻到头了 → 找控制器再要一页更早的（20 条）。
     *
     * 触发条件就是"contentY 贴到顶"：滚轮滚到顶、拖滚动条拖到顶都算。
     */
    function maybeLoadOlder() {
        if (_paging || count === 0 || contentY > 24)
            return
        // 内容还没一屏长：根本没有"往上翻"这回事
        if (atBottom())
            return
        if (!ChatController.historyHasMore || ChatController.historyLoading)
            return

        currentAnchor()
        // 先上闸门再拉：控制器同步 emit messagesChanged，
        // 不挡住就会在同一次 contentY 变化里连拉好几页
        _paging = true
        pageGuard.restart()
        if (ChatController.loadOlderMessages(20) <= 0) {
            releasePaging()
            _anchorIndex = -1
            _anchorOffset = 0
        }
    }

    /// 解锁翻页（加载落定 / 失败 / 兜底超时）
    function releasePaging() {
        _paging = false
        pageGuard.stop()
    }

    /// 记下"视口顶端那条消息"（翻页后要用它把视口摆回原处）
    function currentAnchor() {
        for (let i = 0; i < messageModel.count; ++i) {
            const item = msgColumn.children[i + 1]
            if (item && item.y + item.height > contentY) {
                _anchorIndex = i
                _anchorOffset = item.y - contentY
                return
            }
        }
        _anchorIndex = -1
        _anchorOffset = 0
    }

    /*
     * 头部新增 added 条之后，把 _anchorIndex 那条消息放回原处。
     *
     * 插入时 Column 会整列往下推一页，视口不跟着动就等于"白翻"。
     * 这里等下一帧（Column 排完版，y 才是新值）再按真实几何摆回去。
     */
    function restoreAnchor(added, chBefore) {
        if (_anchorIndex < 0) {
            releasePaging()
            return
        }
        /*
         * 用户正拖着滚动条：这一页是"拖到顶"顺带拉出来的，
         * 位置由鼠标说了算。这时候再把视口摆回锚点，等于
         * 跟用户的手抢方向盘 —— 实测拖到底松手后，一次
         * 翻页的 restore 会把视图拽回锚点（停在 17:06 的
         * 老消息上，而不是最新一条）。
         */
        if (_barScrolling) {
            _anchorIndex = -1
            _anchorOffset = 0
            releasePaging()
            return
        }
        _restoreIndex = Math.min(_anchorIndex + added, count - 1)
        _restoreOffset = _anchorOffset
        _restoreCh = chBefore
        _anchorIndex = -1
        _anchorOffset = 0
        _restoreTries = 0
        restoreTimer.restart()
    }

    function applyRestore() {
        if (_restoreIndex < 0)
            return
        // Column 还没长高 = 这一页还没排进来，等下一帧
        if (contentHeight <= _restoreCh + 0.5 && ++_restoreTries < 8) {
            restoreTimer.restart()
            return
        }
        const item = msgColumn.children[_restoreIndex + 1]
        _restoreIndex = -1
        if (item)
            contentY = Math.max(0, item.y - _restoreOffset)
        syncReadState()
        // 视口已经离开顶部才解锁；还贴着顶就交给 pageGuard 兜底
        if (contentY > 24)
            releasePaging()
    }

    /*
     * 摁到底。Column 的高度是实数，通常一次赋值就到；
     * 但新气泡的高度会晚一帧才落定，所以 snapToBottom 里
     * 留了最多 8 帧的补量（不是 ListView 那种无限追帧）。
     */
    function pinToBottom() {
        followTail = true
        // 先报"在底部"：人正被带到最新一条，这段时间的消息该算已读
        ChatController.chatAtBottom = true
        _snapTries = 0
        Qt.callLater(snapToBottom)
    }

    function snapToBottom() {
        // 这次位移是我们自己造成的：不能让 onContentYChanged 把它
        // 当成"用户离开了底部"，否则 followTail 会被误关掉
        _snapping = true
        contentY = maxContentY()
        _snapping = false
        _lastContentY = contentY

        // 新气泡的高度常常晚一帧才算进 Column，这一拍量到的
        // maxContentY 会短一条 —— 补几帧，和 applyRestore 同理
        if (!atBottom() && followTail && !_barScrolling && _snapTries < 8) {
            ++_snapTries
            snapTimer.restart()
        }
    }

    // 翻页后摆视口：等下一帧再量（Column 这一拍还没排完版）
    Timer {
        id: restoreTimer
        interval: 16
        onTriggered: messageView.applyRestore()
    }

    // 补摁到底：内容高度/位置晚一帧才落定时，下一帧再量一次
    Timer {
        id: snapTimer
        interval: 16
        onTriggered: messageView.snapToBottom()
    }

    // 翻页兜底：400ms 内视口还没摆回去也解锁，
    // 宁可多拉一页也不能让「往上翻」彻底失灵
    Timer {
        id: pageGuard
        interval: 400
        onTriggered: messageView.releasePaging()
    }

    onContentHeightChanged: {
        // 跟随中内容长高了（新消息 / 换会话铺开）→ 补摁到底
        if (followTail && !_barScrolling) {
            Qt.callLater(function () {
                if (messageView.followTail && !messageView._barScrolling)
                    messageView.snapToBottom()
            })
        }
    }

    onContentYChanged: {
        // 记下这一步走了多远，再更新基准（提前做，
        // 免得下面几个 return 让基准过期）
        const delta = contentY - _lastContentY
        _lastContentY = contentY

        // 人在看哪里就报给控制器（决定收到的消息算不算已读）
        syncReadState()

        // 翻到顶了 → 拉更早的一页（拖滚动条到顶也算）
        maybeLoadOlder()

        // 用户正在拖滚动条 / 我们正把自己摁到底：
        // 这些位置变化不代表"用户想停在这"
        if (_barScrolling || _snapping)
            return

        /*
         * 内容一长高，Flickable 会把 contentY 自己抹平零点几像素
         * （实测 499.25 → 499）。这不是用户在滚 —— 当成"离底"
         * 会把 followTail 关掉，随后那一下补帧就被守卫拦下，
         * 新消息永远停在屏幕外面。所以只认真正的一步位移。
         */
        if (Math.abs(delta) < 1)
            return

        followTail = atBottom()
    }

    onHeightChanged: {
        // 视口高度变了（进 / 出选择模式、窗口缩放）：本来在底部就继续待在底部
        if (followTail)
            Qt.callLater(function () { messageView.snapToBottom() })
    }

    // 滚停（滚轮 / 拖动 / 惯性都算）：把视口收回真实范围
    onMovementEnded: {
        settleScroll()
        // 已经在顶上还继续往上滚时 contentY 不变，上面那个信号不会来
        maybeLoadOlder()
    }

    Column {
        id: msgColumn
        width: messageView.width
        spacing: Theme.spacingMd

        // 日期分隔条。真接了历史消息以后按天插，现在只有一段
        Item {
            width: msgColumn.width
            height: 42

            Row {
                anchors.centerIn: parent
                spacing: Theme.spacingSm

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30
                    height: 1
                    color: Theme.lineSoft
                }

                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    // 翻到头了就直说，省得用户以为"往上翻失灵了"
                    // 上面再没有更早的消息时直说；还有就提示"更早" ——
                    // 具体是哪一天由下面第一条的时间标签负责，别在这儿猜
                    text: (!ChatController.historyHasMore && messageView.count > 0)
                          ? "没有更多消息了" : "更早的消息"
                    font: Theme.fontMonoSm
                    color: Theme.muted
                }

                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 30
                    height: 1
                    color: Theme.lineSoft
                }
            }
        }

        Repeater {
            model: messageModel

            /*
             * 一行 = 可选的时间标签 + 气泡。
             *
             * 标签是**默认常显**的（QQ 那种居中灰字），只在
             * "第一条 / 跟上一条隔了 5 分钟以上 / 跨天"时出现；
             * 气泡旁那个 HH:mm 仍然是悬浮 1 秒才显示，两者不冲突。
             */
            delegate: Column {
                width: msgColumn.width
                spacing: Theme.spacingXs

                /*
                 * 群操作的系统消息（服务端 from_uid = 1，api-group.md §9.5）：
                 * 居中灰字提示 —— 不是某个人的气泡，也没有发送者 / 时间标签。
                 * 建群、拉人、踢人、解散都走这一种渲染。
                 */
                Text {
                    visible: system === true
                    width: Math.min(msgColumn.width * 0.82, 520)
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: body
                    font: Theme.fontBody
                    color: Theme.muted
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    topPadding: Theme.spacingSm
                    bottomPadding: Theme.spacingSm
                }

                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    visible: system !== true && text.length > 0
                    text: view.stampFor(index, tsMs)
                    font: Theme.fontMonoSm
                    color: Theme.muted
                    opacity: 0.85
                }

                ChatBubble {
                    visible: system !== true
                    /*
                     * ListModel 的 role 不能叫 text / state ——
                     * 那会和 ChatBubble 自己的同名属性打架。所以
                     * role 名统一加后缀，在这里转接。
                     */
                    width: parent.width
                    fromSelf: fromMe
                    text: body
                    time: clock
                    state: delivery
                    // 群聊里每条消息的发送者不同，名字按 senderUid 解析
                    peerName: fromMe ? LoginController.name
                                     : view.senderLabel(senderId, senderText)
                    peerUid: fromMe ? LoginController.uid
                                    : (senderId.length > 0 ? senderId
                                                           : ChatController.activePeerUid)
                    selfName: LoginController.name
                    selfUid: LoginController.uid

                    // 右键菜单要按页面坐标系定位
                    coordinateSpace: view.menuSpace ? view.menuSpace : view
                    onContextMenuRequested: (x, y) => view.contextMenuRequested(msgId, body, x, y)

                    // 选择模式：整行可点，点一下切换选中
                    selectable: view.selecting
                    selected: view.selectedIds[msgId] === true
                    onToggleRequested: view.toggleRequested(msgId)
                }
            }
        }
    }
}

ChatScrollBar {
    anchors.top: messageView.top
    anchors.bottom: messageView.bottom
    anchors.left: messageView.right
    anchors.leftMargin: Theme.spacingXs
    width: 10
    target: messageView
}

    // ══════════════════════════════════════════════════════════════
    //  时间标签 + 模型同步
    // ══════════════════════════════════════════════════════════════

function messageRow(m) {
    return {
        msgId: String(m.id),
        fromMe: m.fromSelf === true,
        body: m.text !== undefined ? m.text : "",
        clock: m.time !== undefined ? m.time : "",
        delivery: m.state !== undefined ? m.state : "",
        // 毫秒时间戳：只用来判断"要不要在气泡上面插一条时间标签"
        tsMs: Number(m.ts !== undefined ? m.ts : 0),
        // 发送者：群聊气泡上的名字 / 头像底色按它取（单聊为空，退回会话 uid）
        senderId: m.senderUid !== undefined ? String(m.senderUid) : "",
        senderText: m.senderName !== undefined ? String(m.senderName) : "",
        // 群操作系统消息（from_uid = 1）：走居中提示，不渲染成气泡
        system: m.system === true
    }
}

/// 两条消息隔多久以上才插一条时间标签（QQ 的默认节奏是 5 分钟）
readonly property int stampGapMs: 5 * 60 * 1000

/*!
    一条消息的**默认时间标签**（居中、常显，QQ 那种）。

    规则：第一条 / 跟上一条隔了 5 分钟以上 / 跨天 → 显示；挨着的连续聊天不显示
    （每条都戳个时间反而看不清）。取不到时间戳（演示数据）就不显示。
*/
function stampFor(index, ts) {
    if (!ts || ts <= 0)
        return ""

    if (index === 0)
        return formatStamp(ts)

    const prev = messageModel.get(index - 1)
    const prevTs = prev && prev.tsMs ? prev.tsMs : 0
    if (prevTs > 0 && ts - prevTs < stampGapMs && !isOtherDay(prevTs, ts))
        return ""

    return formatStamp(ts)
}

/// 两个时间戳是不是同一天
function isOtherDay(a, b) {
    const da = new Date(a)
    const db = new Date(b)
    return da.getFullYear() !== db.getFullYear()
        || da.getMonth() !== db.getMonth()
        || da.getDate() !== db.getDate()
}

/// 时间戳 → 标签文本：今天只给 HH:mm，更早的带上"昨天 / 月日 / 年月日"
function formatStamp(ms) {
    const when = new Date(ms)
    const now = new Date()
    const pad = (v) => (v < 10 ? "0" + v : String(v))
    const clock = pad(when.getHours()) + ":" + pad(when.getMinutes())

    const startOfToday = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime()
    const startOfYesterday = startOfToday - 24 * 60 * 60 * 1000

    if (ms >= startOfToday)
        return clock
    if (ms >= startOfYesterday)
        return "昨天 " + clock
    if (when.getFullYear() === now.getFullYear())
        return (when.getMonth() + 1) + "月" + when.getDate() + "日 " + clock
    return when.getFullYear() + "年" + (when.getMonth() + 1) + "月"
        + when.getDate() + "日 " + clock
}

/*!
    把 ChatController.messages 同步进 messageModel。

    · **纯追加**（同一会话来了新消息 / 刚发出一条）：只 append 新行，并原地更新
      已有行的状态（sending → sent / failed）。模型不重置，ListView 的 contentY
      原地不动 —— 正在翻历史的人不会被拽走。
    · **头部新增**（往上翻页，更早的一页插进来）：现有模型正好是新列表的
      **后缀**，于是只往头部 insert。重建会把 delegate 全拆了重来，视口当场归零，
      翻页的人会看到屏幕闪一下再跳回去。
    · 其他情况（换会话 / 删除 / 清空 / 首次加载历史）：整体重建。
      重建会丢掉滚动位置，但那几种操作本来就该从头看（换会话后由
      onActiveConversationChanged 统一滚到底）。
    @return 这次往**头部**插了几条（0 = 不是翻页）
*/
function syncMessageModel(list) {
    /*
     * 先认"头部新增"：现有模型 == 新列表的后缀。
     * 从后往前插，插完顺序才是 list[0..add-1]。
     */
    if (messageModel.count > 0 && messageModel.count < list.length) {
        const add = list.length - messageModel.count
        let suffix_same = true
        for (let i = 0; i < messageModel.count; ++i) {
            if (String(messageModel.get(i).msgId) !== String(list[i + add].id)) {
                suffix_same = false
                break
            }
        }

        if (suffix_same) {
            for (let i = add - 1; i >= 0; --i)
                messageModel.insert(0, messageRow(list[i]))
            // 底下那些行的状态也可能刚变（收到回执），顺手原地更新
            for (let i = messageModel.count - 1; i >= add; --i) {
                const state = list[i].state !== undefined ? list[i].state : ""
                if (messageModel.get(i).delivery !== state)
                    messageModel.setProperty(i, "delivery", state)
            }
            return add
        }
    }

    let prefix_same = messageModel.count <= list.length

    if (prefix_same) {
        for (let i = 0; i < messageModel.count; ++i) {
            if (String(messageModel.get(i).msgId) !== String(list[i].id)) {
                prefix_same = false
                break
            }
        }
    }

    if (prefix_same) {
        for (let i = 0; i < messageModel.count; ++i) {
            const state = list[i].state !== undefined ? list[i].state : ""
            if (messageModel.get(i).delivery !== state)
                messageModel.setProperty(i, "delivery", state)
        }
        for (let i = messageModel.count; i < list.length; ++i)
            messageModel.append(messageRow(list[i]))
        return 0
    }

    // 整包重建 = 换了会话 / 换了账号：翻页锚点跟着作废
    messageView._anchorIndex = -1
    messageModel.clear()
    for (let i = 0; i < list.length; ++i)
        messageModel.append(messageRow(list[i]))
    return 0
}

/*!
    发一条消息（ZChatInput 的 sendRequested 与自动化都走这里）。

    控制器返回 false 表示没必要发（空串 / 超长）或没选中会话，这时候
    **不清输入框** —— 用户改两个字还能接着发，清掉等于让他重打一遍。
    成功才用 ZChatInput.clear()（正文 + 残留附件一起清）。
*/
}
