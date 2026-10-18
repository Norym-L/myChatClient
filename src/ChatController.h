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
 * 服务端现状（`GateServer/docs/api-chat.md` + 2026-10-18 晚的三个提交）：
 *   发送 1013/1014、推送 1022、拉取 1023/1024（**PEL 优先重投**）、确认 1025/1026
 *   （`XACKDEL`）都已实现；**所有 handler 都会校验登录态**（未登录回 1011）。
 *   历史记录与群聊未实现。
 *
 * ── 收消息链路（先拉后确认，at-least-once）─────────────────────────
 *   1022 推送（只有"你有新消息"，不含内容）
 *     → 本地攒计数；攒够 10 条或 2s 内没新推送 → 发 1023（count = min(计数, 10)）
 *     → 1024 返回 ≤10 条（**可能包含上次没确认的旧消息**）
 *     → 按 `msgs[].id` **去重**后落进对应会话（置顶 / 未读 +1）
 *     → 发 1025 确认（一次 ≤10 个 id）→ 服务端 XACKDEL 删除
 *   登录成功后也会主动拉一次：离线期间的消息**不会**补推 1022，
 *   而且 PEL 里上次没确认的也会在这一步被重投回来。
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

    [[nodiscard]] QString tip() const { return m_tip; }
    [[nodiscard]] bool tipIsError() const { return m_tip_is_error; }

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

private:
    /// 1013 的结果（由 ChatSocket 的 textMessageResult 转入；回执按 msg_id 认领）
    void onTextMessageResult(bool ok, int error, const QString& msgId,
                             const QString& toUid, const QString& body);

    /// 1024 的结果：把消息落进会话、发确认、视积压情况决定要不要继续拉
    void onChatMessages(const QList<mychat::protocol::ChatMessage>& messages, int error);

    /**
     * 发一次 1023。
     *
     * 条数取 `min(待拉取计数, 10)`；计数为 0（登录后主动拉）就拉满 10 条。
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

    /// ⚠ 演示数据：只有 MYCHAT_DEMO=1 时注入（保留气泡版式自检能力），平时界面无假数据
    void seedDemoData();

    QVariantList m_conversations;
    QHash<QString, QVariantList> m_history;
    /// 已经从本地库加载过历史的会话（空历史也算，避免每次点开都查一遍）
    QSet<QString> m_history_loaded;

    /// 本地聊天记录（SQLite）。登录成功后按 uid 打开；打不开就退化成纯内存
    ChatStore m_store;

    QString m_active_uid;
    QString m_active_name;
    /// 当前会话是不是演示数据（openConversation 时按会话表里的 demo 字段算）
    bool m_active_is_demo = false;

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
};
