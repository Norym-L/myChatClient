#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>

/**
 * ChatController —— 会话 / 消息控制器（QML 与 core 之间的唯一入口）
 *
 * ⚠ 服务端**没有**文本消息：
 *     · `TextChatMsgReq/Rsp`(1013/1014) 与 `NotifyTextChatMsgReq`(1015) 在 const.h 里
 *       有编号，但 ChatServer 没注册处理器 —— 发了也是石沉大海。
 *   但好友列表（1020/1021）已经有了 → **会话列表由好友列表驱动**（见 syncFriends）：
 *   每个好友就是一个会话入口，历史消息为空（点开显示"暂无消息"）。
 *   消息气泡的演示数据只剩 `MYCHAT_DEMO=1` 时才注入（见 seedDemoData），
 *   平时界面上不会再出现假数据。
 *
 * ── 与服务器的接口（服务端补齐后按这里接，改动量很小）──────────────────
 *   发消息   sendText() → 本地追加（state = "local"）→ emit textMessageOutgoing(peerUid, text)
 *           接线点：ChatSocket 加 sendTextMessage() 发 1013；收到 1014 成功回包时
 *           调用 markMessageState(peerUid, msgId, "sent")，失败改成 "failed"。
 *   收消息   ChatSocket 收到 1015 后调用 appendIncoming(peerUid, text, time)
 *           —— 会顺带把会话置顶、未读 +1。
 *   会话列表 好友列表 → ContactController.friendList → 本类 syncFriends()（main.cpp 里接线）。
 *   历史消息 服务端没有"拉历史"接口，每个会话的初始消息是空的。
 *
 * QML 侧：
 *   ChatController.openConversation(uid, name) / sendText(text) / markRead(uid)
 *   读 conversations / messages / activePeerUid / activePeerName /
 *      hasActiveConversation / activePeerIsDemo / serverSupportsText / tip / tipIsError
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
    /// state：demo（演示数据）/ local（只在本机）/ sent / failed（后两者待服务端实现后使用）
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)

    Q_PROPERTY(bool hasActiveConversation READ hasActiveConversation NOTIFY activeConversationChanged)
    Q_PROPERTY(QString activePeerUid READ activePeerUid NOTIFY activeConversationChanged)
    Q_PROPERTY(QString activePeerName READ activePeerName NOTIFY activeConversationChanged)
    /// 当前会话是不是演示数据（MYCHAT_DEMO=1 造出来的那条）—— 界面据此决定要不要挂 DEMO 标签
    Q_PROPERTY(bool activePeerIsDemo READ activePeerIsDemo NOTIFY activeConversationChanged)

    /// 服务端是否已支持文本消息。现在恒为 false —— 界面据此显示"仅本地"提示。
    /// 服务端接好后把这里改成从能力协商/配置读取，UI 不用动。
    Q_PROPERTY(bool serverSupportsText READ serverSupportsText CONSTANT)

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

    [[nodiscard]] bool serverSupportsText() const { return false; }

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
     * 发一条文本。服务端未支持时只做本地回显，并给出明确提示。
     *
     * @return true 表示已经追加到本地（界面可以清空输入框）；
     *         false 表示没发 —— 没有会话 / 空串 / 超长，QML 那边不要清空用户输入。
     */
    Q_INVOKABLE bool sendText(const QString& text);

    /// 清未读
    Q_INVOKABLE void markRead(const QString& peerUid);

    /**
     * 收到别人发来的消息（服务端 NotifyTextChatMsgReq 的落点，**待接**）。
     *
     * 现在没有调用方 —— 预留着，接上以后：会话置顶、未读 +1、当前会话里直接追加。
     */
    void appendIncoming(const QString& peerUid, const QString& text, const QString& time);

    /// 改某条消息的投递状态（sent / failed）。服务端回包后调用。
    void markMessageState(const QString& peerUid, int messageId, const QString& state);

    signals:

    void conversationsChanged();
    void messagesChanged();
    void activeConversationChanged();
    void tipChanged();
    void tipIsErrorChanged();

    /// 有消息要发出去。**服务端接口的接线点**：接上 TextChatMsgReq(1013) 后转发即可。
    void textMessageOutgoing(const QString& peerUid, const QString& text);

private:
    void setTip(const QString& text, bool is_error = false);

    /// 把会话置顶并更新摘要（真实链路里收到消息也该走这一步）
    void touchConversation(const QString& peerUid, const QString& last_text, const QString& last_time);
    /// 往某个会话追加一条消息
    void appendMessage(const QString& peerUid, bool from_self, const QString& text,
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

    int m_next_message_id = 1;
    QString m_tip;
    bool m_tip_is_error = false;
};
