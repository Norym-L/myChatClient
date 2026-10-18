#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QVariantList>

/**
 * ChatController —— 会话 / 消息控制器（QML 与 core 之间的唯一入口）
 *
 * 服务端现状（`GateServer/docs/api-chat.md`，2026-10-18）：
 *    · **发消息**：1013 请求 / 1014 回执 —— 已实现；
 *    · **新消息通知**：1022 推送 —— 已实现，但**推送体不含内容**；
 *    · **消息拉取 / 历史记录**：**未实现**（没有对应 message_id）。
 *   所以：发出去的消息能看到状态（发送中 / 已发送 / 失败），
 *   但**收不到别人的消息内容**，每个会话的历史依然是空的。
 *
 * ── 发送链路 ──────────────────────────────────────────────────────
 *   QML: sendText(text)
 *     → 本地先落一条气泡（state = "sending"），用本地消息 id 生成 msg_id（"c-<id>"）
 *     → ChatSocket::sendTextMessage(msgId, toUid, body)（1013）
 *     → 1014 回显 msg_id → ChatSocket::textMessageResult(ok, error, msgId, ...)
 *     → 按 msgId 精确改那条气泡的状态（sent / failed）
 *   msg_id 是服务端唯一回显的字段，也是**唯一**能把回执对回某条气泡的依据；
 *   别在别处自己拼 msg_id 发 1013，否则本类的状态会留在「发送中」。
 *
 * QML 侧：
 *   ChatController.openConversation(uid, name) / sendText(text) / markRead(uid)
 *   读 conversations / messages / activePeerUid / activePeerName /
 *      hasActiveConversation / activePeerIsDemo / tip / tipIsError
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
     * 收到别人发来的消息内容（**目前没有调用方**）。
     *
     * 服务端 1022 推送不带内容、也没有拉取接口，所以"真正收到一条消息"这件事
     * 现在根本无从触发。等拉取接口（提案 1023/1024）落地后接到这里：
     * 会话置顶、未读 +1、当前会话里直接追加。
     */
    void appendIncoming(const QString& peerUid, const QString& text, const QString& time);

    /// 改某条消息的投递状态（sending / sent / failed）
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

    void setTip(const QString& text, bool is_error = false);

    /// 把会话置顶并更新摘要（真实链路里收到消息也该走这一步）
    void touchConversation(const QString& peerUid, const QString& last_text, const QString& last_time);
    /// 往某个会话追加一条消息，返回它的本地 id（用于后续改状态）
    int appendMessage(const QString& peerUid, bool from_self, const QString& text,
                      const QString& time, const QString& state);
    /// 未读数 -1 之类的小改动，找不到就返回
    [[nodiscard]] int indexOf(const QString& peerUid) const;

    /// ⚠ 演示数据：只有 MYCHAT_DEMO=1 时注入（保留气泡版式自检能力），平时界面无假数据
    void seedDemoData();

    QVariantList m_conversations;
    QHash<QString, QVariantList> m_history;

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

    int m_next_message_id = 1;
    QString m_tip;
    bool m_tip_is_error = false;
};
