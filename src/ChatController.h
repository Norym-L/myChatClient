#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QQueue>
#include <QSet>
#include <QString>
#include <QVariantList>

#include "protocol.h"
#include "ChatStore.h"

class QTimer;

/**
 * ChatController —— 会话 / 消息控制器（QML 与 core 之间的唯一入口）
 *
 * 服务端现状（`GateServer/docs/api-chat.md` + `api-group.md`，2026-10-23 基线 `df647ad`）：
 *   发送 1013/1014、直推 1047、推送 1022、拉取 1023/1024（**PEL 优先重投**）、确认 1025/1026
 *   （`XACKDEL`）都已实现；**所有 handler 都会校验登录态**（未登录回 1011）。
 *   历史记录仍未实现；**群聊已实现**（1033–1042 由 GroupController 管，
 *   群消息复用 1013/1024，本类负责把它落进 `g<gid>` 会话）。
 *
 * ── 群聊在会话层的三条约定 ─────────────────────────────────────────
 *   1. 群会话键是 **`g<gid>`**：群号和用户 uid 都是数字、来自两张表，可能撞号；
 *   2. 群名**只存在本机**（服务端没有群资料查询接口），丢了就显示「群 <gid>」；
 *   3. 服务端会把群消息也写进**发送者自己**的离线流 → 拉到自己的回显要丢弃，
 *      但仍然要 ACK（否则 PEL 会一直重投）；
 *   4. 群操作（建群 / 拉人 / 踢人 / 解散）会写一条 **from_uid = "1"** 的系统消息
 *      （`api-group.md` §9.5，没有独立类型标记，客户端就按这个保留 uid 识别）：
 *      消息项带 `system = true`，界面渲染成居中灰字提示，不当普通气泡；
 *      同时 emit groupSystemMessage(gid, body)，让成员列表在拉人 / 踢人后强刷。
 *
 *   被踢 / 解散后服务端不再校验成员关系，但本机不会删聊天记录：群会话留着，
 *   系统提示就在里面；用户再发消息会收到 1023（旧服务端是 1022），界面照常提示"不在群里"。
 *   主动解散时本地先移除会话 —— 服务端随后推来的「该群聊已解散」不能把它复活
 *   （`m_removed_gids` 挡住）。
 *
 * ── 收消息链路（两条，同时保留）─────────────────────────────────────
 *   ① 1022 推送（只有"你有新消息"，不含内容）→ 1023 拉取 → 1025 确认：
 *     · 1022 推送
 *       → 本地攒计数；攒够 10 条或 2s 内没新推送 → 发 1023（count = min(计数, 10)）
 *       → 1024 返回 ≤10 条（**可能包含上次没确认的旧消息**）
 *       → 按 Stream 编号 **去重**后落进对应会话（置顶 / 未读 +1）
 *       → 发 1025 确认（一次 ≤10 个 id）→ 服务端 XACKDEL 删除
 *     · 登录成功后也会主动拉一次：离线期间的消息**不会**补推 1022，
 *       而且 PEL 里上次没确认的也会在这一步被重投回来。
 *     · 另外登录后每 **30s** 还会兜底拉一次（断线停）：服务端推送链路静默失效时，
 *       客户端仍能在一个周期内看到新消息，不会表现成"重登才恢复"。
 *   ② 1047 直推完整正文（2026-10-24 实验分支）：
 *     · 接收方**在线**时调度器直接推 `group_id / from_uid / type / body / time`；
 *     · 这条消息**不进** `offlinestream_<uid>` → 不用拉、**也不用 ACK**；
 *     · 落库 / 置顶 / 未读 / 系统消息与 ① 共用同一套规则；
 *     · 离线时仍走 ①，所以两条链路必须都在（旧服务端也只发 1022）。
 *
 *   ⚠ 服务端先读 PEL（已投递未确认），PEL 为空才读新消息。未确认的消息会
 *     反复重投，所以**去重是必须的**（`m_seen_message_ids`）；确认失败也不要紧，
 *     下一轮会把它们再送回来。
 *
 * ── 发送链路 ──────────────────────────────────────────────────────
 *   QML: sendText(text)
 *     → 本地先落一条气泡（state = "sending"），用本地消息 id 生成 msg_id（"c-<id>"）
 *     → ChatSocket::sendTextMessage(msgId, toUid, body)（1013）
 *     → 1014 回显 msg_id → ChatSocket::textMessageResult(ok, error, msgId, ...)
 *     → 按 msgId 精确改那条气泡的状态（sent / failed）
 *
 * QML 侧：
 *   ChatController.openConversation(uid, name) / sendText(text) / markRead(uid)
 *   读 conversations / messages / activePeerUid / activePeerName /
 *      hasActiveConversation / activePeerIsDemo / tip / tipIsError
 *
 * ── 本地记录（`ChatStore`，SQLite）──────────────────────────────────
 *   服务端确认即删除、也没有历史接口 → **本地库是唯一的历史来源**。
 *   · 登录成功后按 uid 打开 `chat_<uid>.db`，回填会话列表；
 *   · 打开会话时懒加载最近 200 条；收发消息**先落库**（收到的那批要先落库再 ACK，
 *     反了的话进程崩在中间就既不在服务端也不在本地）；
 *   · 未读 / 已读 / 发送状态一并持久化；`server_id UNIQUE` 兜住跨进程的 PEL 重投；
 *   · 库打不开只是"没有历史"，聊天照常（退化成纯内存）。
 */
class ChatController : public QObject
{
    Q_OBJECT

    /// 会话列表，每项 QVariantMap：uid / name / nick / displayName / icon /
    /// lastText / lastTime / unread / demo
    /// 数据来源是好友列表（main.cpp 把 ContactController.friendList 接到 syncFriends）；
    /// demo = true 的那几条只在 MYCHAT_DEMO=1 时存在。
    Q_PROPERTY(QVariantList conversations READ conversations NOTIFY conversationsChanged)
    Q_PROPERTY(int conversationCount READ conversationCount NOTIFY conversationsChanged)

    /// 当前会话的消息，每项 QVariantMap：id / fromSelf / text / time / state
    /// state：demo（演示数据）/ sending（已发出、等回执）/ sent（服务端已接收）/
    ///        failed（发送失败）/ local（演示会话里的本机回显）
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)

    Q_PROPERTY(bool hasActiveConversation READ hasActiveConversation NOTIFY activeConversationChanged)
    Q_PROPERTY(QString activePeerUid READ activePeerUid NOTIFY activeConversationChanged)
    Q_PROPERTY(QString activePeerName READ activePeerName NOTIFY activeConversationChanged)
    /// 当前会话是不是演示数据（MYCHAT_DEMO=1 造出来的那条）—— 界面据此决定要不要挂 DEMO 标签
    Q_PROPERTY(bool activePeerIsDemo READ activePeerIsDemo NOTIFY activeConversationChanged)

    /**
     * 当前会话是不是**群聊**。
     *
     * 群会话和单聊共用同一套 `conversations` / `messages` / 本地库，区别只在
     * 会话项的 `kind` 字段。界面靠这个属性切换"输入区占位符 / 头部信息 / 成员面板入口"，
     * 不用自己去解析 uid 前缀（群会话的 uid 是 `g<gid>`，见实现文件的说明）。
     */
    Q_PROPERTY(bool activeIsGroup READ activeIsGroup NOTIFY activeConversationChanged)
    /// 当前群号（纯数字串）；单聊时为空
    Q_PROPERTY(QString activeGroupId READ activeGroupId NOTIFY activeConversationChanged)

    /**
     * 窗口在不在前台（main.qml 把 Window.active 绑进来）。
     *
     * 只有"人真的在看"的时候，收到的消息才当场算已读；窗口在后台 / 最小化时
     * 收到的消息照常攒成未读，切回来再清。否则用别的程序时来的消息会被悄悄
     * 标成已读，用户永远看不到红点。
     */
    Q_PROPERTY(bool windowActive READ windowActive WRITE setWindowActive NOTIFY windowActiveChanged)
    /// 「会话」分区现在可见（且窗口在前台）—— ChatPage 维护；停在联系人 / 好友申请页收的消息也算未读
    Q_PROPERTY(bool chatVisible READ chatVisible WRITE setChatVisible NOTIFY chatVisibleChanged)

    /**
     * 消息列表此刻是不是停在最底部（ChatPage 用 messageView 的几何事实写进来）。
     *
     * 「只有滚在底部才算已读」：人往上翻历史时收到的消息**不该**当场标已读 ——
     * 否则一边翻旧消息、一边把新消息悄悄吃掉，用户永远不知道有人找。
     * 事件流：人滚上去 → false → 进来一条 → 未读 +1（侧栏 / 会话行亮红点）
     *        → 人滚回底部（或点「新消息 ↓」）→ true → 顺手 markRead。
     *
     * 注意和 l chatVisible 的区别：那个是"分区 / 窗口"级别的可见，
     * 这个再往下一层 —— 会话开着，但视口停在半截，也算"没在看"。
     */
    Q_PROPERTY(bool chatAtBottom READ chatAtBottom WRITE setChatAtBottom NOTIFY chatAtBottomChanged)

    /**
     * 这个会话还有没有更早的历史没加载（往上翻到头时靠它决定要不要再拉一页）。
     *
     * 历史在本地 SQLite 里：第一次打开会话读最近一批，之后每次翻到头再读 20 条。
     * 一次把几千条全读出来不但慢，ListView 也扛不住 —— 这也是 QQ 那种
     * "滚到顶再加载"的做法。
     */
    Q_PROPERTY(bool historyHasMore READ historyHasMore NOTIFY historyChanged)
    /// 正在读更早的一页（本地读是同步的，这面旗子只是防止 QML 重复触发）
    Q_PROPERTY(bool historyLoading READ historyLoading NOTIFY historyChanged)

    Q_PROPERTY(QString tip READ tip NOTIFY tipChanged)
    Q_PROPERTY(bool tipIsError READ tipIsError NOTIFY tipIsErrorChanged)

public:
    explicit ChatController(QObject* parent = nullptr);

    [[nodiscard]] QVariantList conversations() const { return m_conversations; }
    [[nodiscard]] int conversationCount() const { return static_cast<int>(m_conversations.size()); }
    [[nodiscard]] QVariantList messages() const { return m_history.value(m_active_uid); }

    [[nodiscard]] bool hasActiveConversation() const { return !m_active_uid.isEmpty(); }
    [[nodiscard]] QString activePeerUid() const { return m_active_uid; }
    [[nodiscard]] QString activePeerName() const { return m_active_name; }
    [[nodiscard]] bool activePeerIsDemo() const { return m_active_is_demo; }
    [[nodiscard]] bool activeIsGroup() const { return m_active_is_group; }
    [[nodiscard]] QString activeGroupId() const { return m_active_gid; }

    [[nodiscard]] QString tip() const { return m_tip; }
    [[nodiscard]] bool tipIsError() const { return m_tip_is_error; }

    [[nodiscard]] bool windowActive() const { return m_window_active; }
    void setWindowActive(bool active);
    [[nodiscard]] bool chatVisible() const { return m_chat_visible; }
    void setChatVisible(bool visible);
    [[nodiscard]] bool chatAtBottom() const { return m_chat_at_bottom; }
    void setChatAtBottom(bool atBottom);

    /*!
        清掉控制器提示（tip）。

        页面用 toast 显示瞬时反馈时先清 tip，免得同一件事在底下那行再报一遍。
    */
    Q_INVOKABLE void clearTip();

    [[nodiscard]] bool historyHasMore() const { return m_history_has_more.value(m_active_uid, false); }
    [[nodiscard]] bool historyLoading() const { return m_history_loading; }

    /// 打开某个会话（同时清掉它的未读）
    Q_INVOKABLE void openConversation(const QString& peerUid, const QString& peerName);

    /**
     * 用好友列表重建会话列表（ContactController.friendList 变化时由 main.cpp 调）。
     *
     * 规则：
     *   · 每个好友一个会话，**已有的会话状态（未读 / 摘要）保留**，只更新资料字段；
     *   · 不再是好友的会话被移除（活动会话若在其中，自动关闭）；
     *   · `demo = true` 的演示会话原样保留在末尾（只有 MYCHAT_DEMO=1 时才会有）。
     *
     * @param friends 每项至少含 uid / name，可选 nick / displayName / icon / desc / sex
     */
    Q_INVOKABLE void syncFriends(const QVariantList& friends);

    /**
     * 登记一个群（建群成功后 / 收到这个群的第一条消息时）。
     *
     * 会话键是 **`g<gid>`**（如 `g20001`）—— 群 id 与用户 uid 都是数字，
     * 来自两张表、可能撞号，不加前缀会把群消息落进同号的用户会话里。
     *
     * 群名只能本地存：服务端**没有群资料查询接口**，`api-group.md` §11.8。
     * 没有名字时界面显示「群 <gid>」，不要让会话行空着。
     *
     * @param gid  群号（纯数字串）
     * @param name 群名（可空）
     * @param role 我在群里的角色（2 群主 / 1 管理员 / 0 成员）；建群时传 2，
     *             从消息里发现群时不知道，传 0
     */
    Q_INVOKABLE void registerGroup(const QString& gid, const QString& name, int role = 0);

    /**
     * 解散 / 被移出后删掉本地群会话（含本地库里的会话行与群资料）。
     *
     * ⚠ 只删**会话与群资料**，聊天记录保留 —— 和服务端"解散不清理任何客户端记录"
     *   的行为对齐；用户想彻底清掉可以走「清空聊天记录」。
     */
    Q_INVOKABLE void removeGroupConversation(const QString& gid);

    /// 关掉当前会话（回到"未选择"空态）
    Q_INVOKABLE void closeConversation();

    /**
     * 发一条文本（走真实链路：1013 → 1014）。
     *
     * @return true 表示已经落了一条气泡（界面可以清空输入框），
     *         此时投递结果可能是 sending → sent / failed，看 messages 里的 state；
     *         false 表示没发 —— 没有会话 / 空串 / 超长，QML 那边不要清空用户输入。
     */
    Q_INVOKABLE bool sendText(const QString& text);

    /// 清未读
    Q_INVOKABLE void markRead(const QString& peerUid);

    /**
     * 往上翻页：再读 \a count 条更早的历史，拼到当前消息列表**前面**。
     *
     * 读进来的行会 emit messagesChanged（QML 的 syncMessageModel 认得出这是"头部新增"，
     * 只 insert 不重建，并把视口钉回原来那条消息上）。
     *
     * @return 这次真的加进来几条；0 = 没有更早的了 / 本地库不可用 / 正在读 / 没会话
     */
    Q_INVOKABLE int loadOlderMessages(int count = 20);

    /**
     * 删除若干条消息（**只删本地**）。
     *
     * 服务端没有"删除消息"接口 —— 消息在确认（1025）时就被 XACKDEL 删掉了，
     * 所以这里删的只是本机这份记录，**对方那边完全不受影响**、也删不掉。
     *
     * @param ids 消息 id（`messages[].id`，本地库自增 id）
     * @return 实际删掉的条数；0 = 没找到 / 本地库不可用
     */
    Q_INVOKABLE int deleteMessages(const QVariantList& ids);

    /**
     * 清空与某人的聊天记录（**只清本地**）。
     *
     * 会话本身保留（还在好友列表里就继续显示空会话），只是消息与摘要没了。
     */
    Q_INVOKABLE void clearConversation(const QString& peerUid);

    /**
     * 收到别人发来的**单条**消息内容。
     *
     * 现在收消息走的是 `onChatMessages()` 的**批量**路径（一次拉最多 10 条，
     * 只发一轮信号）；这个单条口子留作外部注入 / 调试，业务链路里没有调用方。
     * 会顺带把会话置顶、未读 +1。
     */
    void appendIncoming(const QString& peerUid, const QString& text, const QString& time);

    /// 改某条消息的投递状态（sending / sent / failed），同时写回本地库
    void markMessageState(const QString& peerUid, int messageId, const QString& state);

    signals:

    void conversationsChanged();
    void messagesChanged();
    void activeConversationChanged();
    void tipChanged();
    void tipIsErrorChanged();
    void windowActiveChanged();
    void chatVisibleChanged();
    void chatAtBottomChanged();
    void historyChanged();

    /**
     * 发消息被服务端判成"不是好友"（1014 的 error = 1022）。
     *
     * 服务端**不推送**「对方删了你」的通知，所以这是被删除方唯一能自动察觉的时机：
     * 界面收到后静默刷新一次好友列表（1020），列表 / 会话跟着收敛。
     *
     * ⚠ 群消息"我不在群里"在 2026-10-24 起改用 1023（`GROUP_ID_INVALID`），
     *   不再复用 1022 —— 那种情况只给提示，不发本信号（刷好友列表救不了群）。
     */
    void peerIsNotFriend(const QString& peerUid);

    /**
     * 收到群操作的系统消息（服务端 `from_uid = "1"`，`api-group.md` §9.5）。
     *
     * 服务端**不推送**成员变化，只在群操作后写一条系统消息；界面靠它知道
     * 「邀请了 N 个好友 / 踢出了 uid / 该群聊已解散」，并顺带强刷成员列表。
     *
     * @param gid  群号
     * @param body 系统消息正文（建群「群聊 <gid> 已创建」、拉人「邀请了 N 个好友」、
     *             踢人「踢出了 <uid>」、解散「该群聊已解散」）
     */
    void groupSystemMessage(const QString& gid, const QString& body);

private:
    /// 1013 的结果（由 ChatSocket 的 textMessageResult 转入；回执按 msg_id 认领）
    void onTextMessageResult(bool ok, int error, const QString& msgId,
                             const QString& toUid, const QString& body);

    /// 1024 的结果：把消息落进会话、发确认、视积压情况决定要不要继续拉
    void onChatMessages(const QList<mychat::protocol::ChatMessage>& messages, int error);

    /**
     * 1047 直推的一条消息（服务端调度器在线投递）。
     *
     * 与 onChatMessages 走同一套落地规则，但**不发 ACK、不触发拉取**：
     * 这条消息不在离线流的 PEL 里。重复 / 自己的群回显 / 无法归属的群消息直接丢。
     */
    void onPushedMessage(const mychat::protocol::ChatMessage& message);

    /// 落一条收到的消息的三种结局（1024 批量与 1047 单条共用）
    enum class LandStatus
    {
        /// 已落库并上屏，调用方按 peer 记未读
        Landed,
        /// 重复：去重表命中，或本地库已有同一个 server_id（PEL 重投）
        Duplicate,
        /// 不上屏：自己发的群消息回显 / 群消息缺 group_id / 本机已移除的群
        Dropped,
    };

    /**
     * 把一条收到的消息落进对应会话（建会话 / 落库 / 置顶 / 系统消息）。
     *
     * @param peer 归属的会话键（单聊 `uid`，群聊 `g<gid>`）；没落下来时为空
     * @return Landed 才该记未读；**Duplicate / Dropped 在批量路径里仍然要 ACK**
     */
    LandStatus landIncomingMessage(const mychat::protocol::ChatMessage& message, QString* peer);

    /// 某个会话未读 +1（内存 + 本地库）
    void bumpUnread(const QString& peerUid);

    /**
     * 发一次 1023。
     *
     * 条数取 `min(待拉取计数, 10)`；计数为 0（登录后主动拉 / 30s 兜底拉）就拉满 10 条。
     * 若已有一个拉取在飞，只记下"还想再拉"（\l m_pull_again），等回包后补。
     */
    void requestPull();

    /**
     * 记下一条已经落过库的消息 id。
     *
     * @return false = 重复（PEL 重投），这条**不该再显示**；但它仍然要被 ACK，
     *         否则服务端会一直把它重投回来。
     */
    bool markMessageSeen(const QString& id);

    void setTip(const QString& text, bool is_error = false);

    /// 登录后按 uid 打开本地库并回填会话列表（失败只是没有历史，不影响聊天）
    void openStore(std::uint64_t uid);
    /// 打开会话时懒加载历史（同一会话只查一次库）
    void ensureHistoryLoaded(const QString& peerUid);
    /// 把某个会话的摘要 / 未读写回本地库
    void persistConversation(const QString& peerUid);
    /// 删掉消息后按库里剩下的最后一条重算会话摘要
    void refreshConversationSummary(const QString& peerUid);

    /// 会话不存在就建一条空会话（收到陌生人消息时用；服务端不校验好友关系）
    void ensureConversation(const QString& peerUid);

    // ── 群会话的键约定 ────────────────────────────────────────────────
    //  群 id 与用户 uid 都是数字，来自两张表、可能撞号（群 20001 vs 用户 20001）。
    //  所以群会话在**内存与本地库**里统一用 `g<gid>` 作键，界面上显示的仍然是
    //  `gid` 本身。三个小工具放这里，避免各处手拼前缀拼错。
    static QString groupKey(const QString& gid) { return QStringLiteral("g") + gid; }
    static bool isGroupKey(const QString& key) { return key.startsWith(QLatin1Char('g')); }
    static QString groupIdOfKey(const QString& key)
    {
        return isGroupKey(key) ? key.mid(1) : QString();
    }

    /// 群会话不存在就建一条（群名取本地库 / 没有就叫「群 <gid>」）
    void ensureGroupConversation(const QString& gid, const QString& name = QString());

    /// 登录成功的 uid（字符串）。群消息要靠它认"这条是不是我自己发的回显"
    QString m_self_uid;

    /**
     * 某个 uid 的显示名（好友备注优先，其次昵称，最后 uid）。
     *
     * 群聊里每条气泡的发送者不同，QML 需要"这条是谁说的"——
     * 名字在 C++ 算好塞进消息项（`senderName`），QML 只负责显示。
     * 群昵称（1042 的 `nick_g`）在 GroupController 手里，QML 会优先用那个。
     */
    [[nodiscard]] QString senderNameFor(const QString& uid) const;

    /// 把会话置顶并更新摘要（真实链路里收到消息也该走这一步）
    void touchConversation(const QString& peerUid, const QString& last_text, const QString& last_time);

    /**
     * 往某个会话追加一条消息（内存 + 本地库）。
     *
     * @param created_ms 消息时间（毫秒）。0 = 用当前时间；收到的消息用 Stream ID 里的时间戳
     * @param server_id  服务端消息编号（收到的消息才有）—— 库里的 UNIQUE 键，重投时挡住重复
     * @param from_uid   发送者 uid（自己发的填空）
     * @return 消息 id（= 库里的自增 id，用于后续改状态）；
     *         **0 表示重复**（库里有同一个 server_id，PEL 重投），调用方不该显示它
     */
    int appendMessage(const QString& peerUid, bool from_self, const QString& text,
                      const QString& time, const QString& state,
                      qint64 created_ms = 0, const QString& server_id = QString(),
                      const QString& from_uid = QString());

    /// 未读数 -1 之类的小改动，找不到就返回
    [[nodiscard]] int indexOf(const QString& peerUid) const;

    /// 人此刻是不是正在看这个会话：窗口在前台 **且** 停在「会话」分区 **且** 视口在底部
    [[nodiscard]] bool attention() const { return m_window_active && m_chat_visible && m_chat_at_bottom; }

    /// ⚠ 演示数据：只有 MYCHAT_DEMO=1 时注入（保留气泡版式自检能力），平时界面无假数据
    void seedDemoData();

    QVariantList m_conversations;
    QHash<QString, QVariantList> m_history;
    /// 已经从本地库加载过历史的会话（空历史也算，避免每次点开都查一遍）
    QSet<QString> m_history_loaded;

    /**
     * uid -> 显示名（好友备注优先）。
     *
     * 只用于**群聊气泡的发送者名字**：单聊每条都是同一个人，界面用会话名就行。
     * 由 syncFriends 维护；群昵称在 GroupController 那边，QML 优先用群昵称。
     */
    QHash<QString, QString> m_friend_names;

    /// 每个会话还有没有更早的历史：首次加载没读满一页就说明到头了
    QHash<QString, bool> m_history_has_more;
    bool m_history_loading = false;

    /// 本地聊天记录（SQLite）。登录成功后按 uid 打开；打不开就退化成纯内存
    ChatStore m_store;

    QString m_active_uid;
    QString m_active_name;
    /// 当前会话是不是演示数据（openConversation 时按会话表里的 demo 字段算）
    bool m_active_is_demo = false;
    /// 当前会话是不是群聊 / 群号（openConversation 时按会话表里的 kind / gid 算）
    bool m_active_is_group = false;
    QString m_active_gid;

    /**
     * 本机已主动移除的群（解散 / 被移出后由界面调 removeGroupConversation）。
     *
     * 服务端解散群时**先写系统消息、再 DEL**（api-group.md §7），所以客户端可能
     * 在会话已删掉之后才拉到「该群聊已解散」—— 不加这道闸会把会话重新建出来。
     * 只记内存：重启后服务端不会再给已解散的群写消息，没必要持久化。
     */
    QSet<QString> m_removed_gids;

    /**
     * 已经发出去、还没等到回执的消息，**按 msg_id 索引**。
     *
     * 1014 会把请求里的 msg_id 原样回显，所以不用再赌发送顺序：
     * 回执里带哪个 id，就改哪个气泡。断线时用它把所有「发送中」标红。
     */
    struct PendingSend
    {
        QString peer_uid;
        int message_id = 0; // 本地气泡 id（不是服务端消息编号）
    };

    QHash<QString, PendingSend> m_sending;

    /*
     * 拉取节流。
     *
     * 1022 每条都会来一次，但拉取是"批量"的（单次 ≤10）—— 每来一条就拉一次
     * 等于把服务端当轮询机用。规则照文档 §3.1：
     *   · 攒够 10 条（> 10）→ 立刻拉；
     *   · 否则等 2s，期间没有新推送就拉。
     */
    QTimer* m_pull_timer = nullptr;

    /**
     * 兜底拉取定时器：登录后每 **30s** 主动发一次 1023（不依赖 1022）。
     *
     * 推送链路静默失效时（1022 丢了、服务端在线状态与会话映射不一致等），
     * 只靠推送触发拉取会"能发不能收"；这层保底让最坏情况也只晚一个周期。
     * 断线停、重登启；主动退出登录不发 linkError，下次 tick 见未登录会自停。
     * 周期可用 `MYCHAT_FALLBACK_PULL_MS` 覆盖（自检用）。
     */
    QTimer* m_fallback_pull_timer = nullptr;
    /// 距离上次拉取，又收到了多少条 1022
    int m_pull_hint = 0;
    /// 拉取在飞时又想来一次 → 等回包后补，别丢
    bool m_pull_again = false;
    /// 最近一次拉取请求的条数：回来的条数达到它说明只拉了一半，还要继续拉
    int m_pull_count = 0;
    /// 确认连续失败次数（消息会留在 PEL 被重投，重试几次就够了，别无限空转）
    int m_ack_retry = 0;

    /*
     * 去重表：服务端 PEL 优先重投（at-least-once），同一条消息可能被拉到多次。
     * 只记内存、**不落盘** —— 进程重启后 PEL 里还没确认的消息本来就该重新显示，
     * 那时候"重复"其实是"找回"。上限 2000 条（一个会话窗口的量级），
     * 超了淘汰最老的 id；真出现更老的重投，最多多显示一条，不会乱。
     */
    QSet<QString> m_seen_message_ids;
    QQueue<QString> m_seen_message_order;

    int m_next_message_id = 1;
    QString m_tip;
    bool m_tip_is_error = false;

    /// 窗口在前台（默认 true：还没绑 QML 也不会把红点全吞掉）
    bool m_window_active = true;
    /// 「会话」分区可见（默认 true，理由同上）
    bool m_chat_visible = true;
    /// 消息列表停在底部（默认 true：还没绑 QML 时按"在看最底下"处理，别把红点全吞掉）
    bool m_chat_at_bottom = true;
};