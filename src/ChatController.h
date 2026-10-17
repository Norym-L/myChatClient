#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QVariantList>

/**
 * ChatController —— 会话 / 消息控制器（QML 与 core 之间的唯一入口）
 *
 * ⚠ 服务端目前**没有**文本消息，也没有好友列表：
 *     · `TextChatMsgReq/Rsp`(1013/1014) 与 `NotifyTextChatMsgReq`(1015) 在 const.h 里
 *       有编号，但 ChatServer 没注册处理器 —— 发了也是石沉大海；
 *     · `getUserApplyListAndFriendList` 是空函数，拿不到好友 → 也列不出真实会话。
 *   所以本控制器现在提供的是**演示数据**，界面会明确标注「DEMO」，
 *   不要让用户以为消息真的发出去了。
 *
 * ── 与服务器的接口（服务端补齐后按这里接，改动量很小）──────────────────
 *   发消息   sendText() → 本地追加（state = "local"）→ emit textMessageOutgoing(peerUid, text)
 *           接线点：ChatSocket 加 sendTextMessage() 发 1013；收到 1014 成功回包时
 *           调用 markMessageState(peerUid, msgId, "sent")，失败改成 "failed"。
 *   收消息   ChatSocket 收到 1015 后调用 appendIncoming(peerUid, text, time)
 *           —— 会顺带把会话置顶、未读 +1。
 *   会话列表 好友列表接口补齐后，把好友转成 conversations 的字段（uid/name/...）即可；
 *           现在这份是 seedDemoData() 造出来的。
 *   历史消息 服务端没有"拉历史"接口，现在每个会话的初始消息也是造出来的。
 *
 * QML 侧：
 *   ChatController.openConversation(uid, name) / sendText(text) / markRead(uid)
 *   读 conversations / messages / activePeerUid / activePeerName /
 *      hasActiveConversation / serverSupportsText / tip / tipIsError
 */
class ChatController : public QObject
{
    Q_OBJECT

    /// 会话列表，每项 QVariantMap：uid / name / lastText / lastTime / unread
    Q_PROPERTY(QVariantList conversations READ conversations NOTIFY conversationsChanged)
    Q_PROPERTY(int conversationCount READ conversationCount NOTIFY conversationsChanged)

    /// 当前会话的消息，每项 QVariantMap：id / fromSelf / text / time / state
    /// state：demo（演示数据）/ local（只在本机）/ sent / failed（后两者待服务端实现后使用）
    Q_PROPERTY(QVariantList messages READ messages NOTIFY messagesChanged)

    Q_PROPERTY(bool hasActiveConversation READ hasActiveConversation NOTIFY activeConversationChanged)
    Q_PROPERTY(QString activePeerUid READ activePeerUid NOTIFY activeConversationChanged)
    Q_PROPERTY(QString activePeerName READ activePeerName NOTIFY activeConversationChanged)

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

    [[nodiscard]] bool serverSupportsText() const { return false; }

    [[nodiscard]] QString tip() const { return m_tip; }
    [[nodiscard]] bool tipIsError() const { return m_tip_is_error; }

    /// 打开某个会话（同时清掉它的未读）
    Q_INVOKABLE void openConversation(const QString& peerUid, const QString& peerName);

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

    /// ⚠ 演示数据：服务端有接口后整段删掉，换成真实好友列表 + 历史消息
    void seedDemoData();

    QVariantList m_conversations;
    QHash<QString, QVariantList> m_history;

    QString m_active_uid;
    QString m_active_name;

    int m_next_message_id = 1;
    QString m_tip;
    bool m_tip_is_error = false;
};
