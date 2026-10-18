#include "ChatController.h"

#include <QTime>
#include <QVariantMap>
#include <QtGlobal>

#include <utility>

#include "ChatSocket.h"
#include "logger.h"
#include "protocol.h"

namespace
{
using mychat::protocol::ErrorCode;
constexpr int kSuccess = static_cast<int>(ErrorCode::Success);

/// 单条消息的长度上限（字符数）。
///
/// 服务端单帧 body 硬上限 2048 字节（`MAX_LENGTH`，超了直接关会话），
/// 而 `maxLength` 也是 QML 输入框的可见上限 —— 两个数要保持一致，别再各写一个。
constexpr int kMaxTextLength = 500;

/**
 * 单条消息的**字节**上限。
 *
 * 光卡 500 字符不够：一个字 3 字节、一个 emoji 4 字节，500 个就是 2000 字节，
 * 再加上 `{"to_uid":"...","type":"1","body":"..."}` 的壳就贴到 2048 了。
 * 留 128 字节余量给 JSON 壳和更长的 uid，超了在这里就说清楚，
 * 别让 ChatSocket 的 sendFrame 静默拒发（那会表现成"消息没发出去还没提示"）。
 */
constexpr int kMaxBodyBytes = mychat::protocol::MAX_BODY_LEN - 128;

QString nowText()
{
    return QTime::currentTime().toString(QStringLiteral("HH:mm"));
}

QVariantMap makeMessage(const int id, const bool from_self, const QString& text,
                        const QString& time, const QString& state)
{
    QVariantMap item;
    item.insert(QStringLiteral("id"), id);
    item.insert(QStringLiteral("fromSelf"), from_self);
    item.insert(QStringLiteral("text"), text);
    item.insert(QStringLiteral("time"), time);
    item.insert(QStringLiteral("state"), state);
    return item;
}
} // namespace

ChatController::ChatController(QObject* parent)
    : QObject(parent)
{
    /*
     * 演示数据默认**不注入**：会话列表现在由好友列表驱动（syncFriends），
     * 真实好友已经有内容可看。只有需要核对气泡 / 列表版式时才开
     * MYCHAT_DEMO=1（无头抓图自检用），避免平时界面上出现假会话。
     */
    if (qEnvironmentVariable("MYCHAT_DEMO") == QLatin1String("1"))
        seedDemoData();

    auto& socket = mychat::ChatSocket::instance();

    // 1013 的结果：按 FIFO 认领（1014 不回消息编号，这是唯一的配对方式）
    connect(&socket, &mychat::ChatSocket::textMessageResult,
            this, &ChatController::onTextMessageResult);

    /*
     * 1022 推送：只知道"有新消息"，**拿不到内容**（服务端没有拉取接口）。
     * 这里只给一句如实的提示 —— 不要凭空造一个气泡，那是在编数据。
     */
    connect(&socket, &mychat::ChatSocket::messageReceived, this, [this]()
                {
                    setTip(QStringLiteral("对方发来新消息；服务端暂未提供消息拉取接口，看不到内容"));
                });

    /*
     * 断线：在飞的消息不会再有回执（ChatSocket 断线时直接清空队列，不补报超时），
     * 所以必须在这里把它们标红并销账，否则界面上会永远停在「发送中」。
     */
    connect(&socket, &mychat::ChatSocket::linkError, this, [this](const QString&)
                {
                    if (m_sending.isEmpty())
                        return;

                    for (const PendingSend& entry: std::as_const(m_sending))
                        markMessageState(entry.peer_uid, entry.message_id, QStringLiteral("failed"));

                    m_sending.clear();
                    setTip(QStringLiteral("连接已断开，发送中的消息未送达"), true);
                });
}

int ChatController::indexOf(const QString& peerUid) const
{
    for (int i = 0; i < m_conversations.size(); ++i)
    {
        if (m_conversations.at(i).toMap().value(QStringLiteral("uid")).toString() == peerUid)
            return i;
    }
    return -1;
}

void ChatController::seedDemoData()
{
    /*
     * ⚠ 演示数据 —— 只在 MYCHAT_DEMO=1 时注入（见构造函数）。
     *
     * 之所以还留着：会话列表 / 气泡 / 输入区这些版式，没有内容根本看不出对不对。
     * 每项都带 demo = true，界面据此挂 DEMO 标签，syncFriends() 也不会把它们清掉。
     */
    struct DemoConv
    {
        const char* uid;
        const char* name;
        const char* last_text;
        const char* last_time;
        int unread;
    };

    const DemoConv conversations[] = {
        {"666673", "三花木尼", "接口我先按 1013/1014 预留，等你那边确认", "11:42", 2},
        {"666674", "agent231903", "好，我先把样式搭出来", "10:18", 0},
        {"1001", "norym", "晚上八点对一下协议文档", "昨天", 1},
        {"666671", "norrmm", "收到", "昨天", 0},
    };

    for (const DemoConv& c: conversations)
    {
        QVariantMap item;
        item.insert(QStringLiteral("uid"), QString::fromUtf8(c.uid));
        item.insert(QStringLiteral("name"), QString::fromUtf8(c.name));
        item.insert(QStringLiteral("nick"), QString());
        item.insert(QStringLiteral("displayName"), QString::fromUtf8(c.name));
        item.insert(QStringLiteral("icon"), QString());
        item.insert(QStringLiteral("desc"), QString());
        item.insert(QStringLiteral("sex"), 2);
        item.insert(QStringLiteral("lastText"), QString::fromUtf8(c.last_text));
        item.insert(QStringLiteral("lastTime"), QString::fromUtf8(c.last_time));
        item.insert(QStringLiteral("unread"), c.unread);
        item.insert(QStringLiteral("demo"), true);
        m_conversations.append(item);
    }

    const auto demo = [this](const char* uid, QList<QPair<bool, QString> > lines)
        {
            QVariantList list;
            for (const auto& [from_self, text]: lines)
            {
                list.append(makeMessage(m_next_message_id++, from_self, text,
                                        QStringLiteral("11:3%1").arg(list.size()), QStringLiteral("demo")));
            }
            m_history.insert(QString::fromUtf8(uid), list);
        };

    demo("666673",
         {
             {false, QStringLiteral("在吗？")},
             {true, QStringLiteral("在，刚把好友申请的推送接完")},
             {false, QStringLiteral("服务端的文本消息还没动吧？")},
             {true, QStringLiteral("没有，TextChatMsg 还是空的。我先把界面和接口预留出来。")},
             {false, QStringLiteral("接口我先按 1013/1014 预留，等你那边确认")}
         });

    demo("666674", {
             {false, QStringLiteral("新界面什么时候能看？")},
             {true, QStringLiteral("好，我先把样式搭出来")}
         });

    demo("1001", {
             {false, QStringLiteral("协议文档里 message_id 的起始值要再核一遍")},
             {true, QStringLiteral("以 const.h 为准，文档写的是 0 起，实际是 1001 起")}
         });

    demo("666671", {{false, QStringLiteral("收到")}});
}

void ChatController::openConversation(const QString& peerUid, const QString& peerName)
{
    if (peerUid.isEmpty())
        return;

    const bool changed = (peerUid != m_active_uid);
    m_active_uid = peerUid;
    m_active_name = peerName;

    // DEMO 标签要跟着会话走：点开演示会话才挂标，点开真实好友不该挂
    const int index = indexOf(peerUid);
    m_active_is_demo = index >= 0 && m_conversations.at(index).toMap()
                                                    .value(QStringLiteral("demo"))
                                                    .toBool();

    markRead(peerUid);

    if (changed)
        emit activeConversationChanged();
    // 同一个会话重复点开也要刷一次消息（真实链路里这里该去拉历史）
    emit messagesChanged();
}

void ChatController::syncFriends(const QVariantList& friends)
{
    /*
     * 会话列表 = 好友列表（投影），但**不能直接替换**：
     *   已有会话的未读 / 摘要 / 顺序是会话状态，重建会把它们清掉。
     * 所以按 uid 做一次合并：资料字段用新的，会话状态沿用旧的。
     *
     * demo = true 的那几条（只有 MYCHAT_DEMO=1 才有）原样追加到末尾 ——
     * 它们是版式自检数据，不该被真实好友列表冲掉。
     */
    QVariantList next;
    next.reserve(friends.size() + m_conversations.size());

    for (const QVariant& entry: friends)
    {
        const QVariantMap f = entry.toMap();
        const QString uid = f.value(QStringLiteral("uid")).toString();
        if (uid.isEmpty())
            continue;

        QVariantMap conv;
        conv.insert(QStringLiteral("uid"), uid);
        conv.insert(QStringLiteral("name"), f.value(QStringLiteral("name")));
        conv.insert(QStringLiteral("nick"), f.value(QStringLiteral("nick")));
        conv.insert(QStringLiteral("displayName"), f.value(QStringLiteral("displayName")));
        conv.insert(QStringLiteral("icon"), f.value(QStringLiteral("icon")));
        conv.insert(QStringLiteral("desc"), f.value(QStringLiteral("desc")));
        conv.insert(QStringLiteral("sex"), f.value(QStringLiteral("sex")));
        conv.insert(QStringLiteral("demo"), false);

        // 会话状态沿用上一个版本的（首次出现就是空的）
        const int old = indexOf(uid);
        const QVariantMap prev = old >= 0 ? m_conversations.at(old).toMap() : QVariantMap();
        // ⚠ 一定要给默认值：QVariantMap 里插一个无效 QVariant，QML 那边读到的就是
        //   undefined → ConversationRow 的 QString 属性会刷 "Unable to assign [undefined]"
        conv.insert(QStringLiteral("lastText"), prev.value(QStringLiteral("lastText"), QString()));
        conv.insert(QStringLiteral("lastTime"), prev.value(QStringLiteral("lastTime"), QString()));
        conv.insert(QStringLiteral("unread"), prev.value(QStringLiteral("unread"), 0));

        next.append(conv);
    }

    for (const QVariant& entry: m_conversations)
    {
        if (entry.toMap().value(QStringLiteral("demo")).toBool())
            next.append(entry);
    }

    m_conversations = next;

    /*
     * 活动会话还在不在？不在（好友被删 / 首次同步）就关掉，别留着对空 uid 发呆。
     *
     * ⚠ 先把"发信号之前有没有活动会话"记下来：conversationsChanged 的订阅方
     *   （ChatPage）会在信号里**自动打开第一个会话**，如果这里在 emit 之后才判断
     *   "原来是不是没有会话"，就会把订阅方刚打开的那个立刻关掉 ——
     *   症状是列表里有好友、右侧却停在「未选择会话」，而且日志一切正常。
     */
    const bool had_active = !m_active_uid.isEmpty();
    bool active_alive = false;
    if (had_active)
    {
        const int index = indexOf(m_active_uid);
        if (index >= 0)
        {
            active_alive = true;
            // 备注可能刚改过：把显示名同步过来，否则会话头还挂着旧名字
            m_active_name = m_conversations.at(index).toMap()
                                           .value(QStringLiteral("displayName"))
                                           .toString();
            m_active_is_demo = m_conversations.at(index).toMap()
                                              .value(QStringLiteral("demo"))
                                              .toBool();
        }
    }

    emit conversationsChanged();

    if (had_active && !active_alive)
        closeConversation();
    else if (active_alive)
        emit activeConversationChanged(); // 名字 / demo 标记可能变了

    MCLOG_INFO() << "[chat] 会话列表已按好友列表同步, 好友" << friends.size()
        << "位, 会话" << m_conversations.size() << "条";
}

void ChatController::closeConversation()
{
    if (m_active_uid.isEmpty())
        return;

    m_active_uid.clear();
    m_active_name.clear();
    m_active_is_demo = false;
    emit activeConversationChanged();
    emit messagesChanged();
}

void ChatController::markRead(const QString& peerUid)
{
    const int index = indexOf(peerUid);
    if (index < 0)
        return;

    QVariantMap item = m_conversations.at(index).toMap();
    if (item.value(QStringLiteral("unread")).toInt() == 0)
        return;

    item.insert(QStringLiteral("unread"), 0);
    m_conversations[index] = item;
    emit conversationsChanged();
}

bool ChatController::sendText(const QString& text)
{
    if (m_active_uid.isEmpty())
    {
        setTip(QStringLiteral("请先选择一个会话"), true);
        return false;
    }

    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return false; // 空消息直接忽略：不发帧、不报错、不加气泡

    if (trimmed.size() > kMaxTextLength)
    {
        setTip(QStringLiteral("单条消息最多 %1 字").arg(kMaxTextLength), true);
        return false;
    }

    if (trimmed.toUtf8().size() > kMaxBodyBytes)
    {
        // 中文 / emoji 一个字占 3~4 字节：500 字也可能顶到服务端 2KB 的单帧上限
        setTip(QStringLiteral("这条消息太长（服务端单帧上限 2KB），请缩短后再发"), true);
        return false;
    }

    const QString time = nowText();

    /*
     * 演示会话（只有 MYCHAT_DEMO=1 才有）里的 uid 是编的。
     *
     * 服务端**不校验好友关系**，真发出去就会写进那个 uid 的离线消息流 ——
     * 等于往一个陌生人（或根本不存在的 uid）那里塞消息。所以演示会话只做本地回显，
     * 状态标 local（界面上显示「仅本地」）。
     */
    if (m_active_is_demo)
    {
        appendMessage(m_active_uid, true, trimmed, time, QStringLiteral("local"));
        touchConversation(m_active_uid, trimmed, time);

        emit messagesChanged();
        emit conversationsChanged();
        setTip(QStringLiteral("演示会话不会发往服务器"));
        return true;
    }

    /*
     * 先落一条 "sending" 的气泡，再把消息交给 ChatSocket。
     * 顺序不能反：sendTextMessage() 在"连接已断 / body 超长"时会**同步**报失败，
     * 那时候必须已经登记过 m_sending，否则那条结果无人认领（气泡永远转圈）。
     */
    const int message_id = appendMessage(m_active_uid, true, trimmed, time,
                                         QStringLiteral("sending"));

    /*
     * msg_id = 本地气泡 id（`c-12` 这种形式）。
     *
     * 服务端不解析它、只原样回显在 1014 里 —— 于是"哪条回执对应哪个气泡"
     * 变成精确匹配，不再依赖发送顺序。用本地 id 的好处是断线/重连后一眼能对上日志。
     */
    const QString msg_id = QStringLiteral("c-%1").arg(message_id);
    m_sending.insert(msg_id, PendingSend{m_active_uid, message_id});

    touchConversation(m_active_uid, trimmed, time);
    emit messagesChanged();
    emit conversationsChanged();

    if (!mychat::ChatSocket::instance().sendTextMessage(msg_id, m_active_uid, trimmed))
    {
        // 没排进队列 = 不会有回执：自己把这条标红销账。
        // 具体原因（未连接 / 未登录 / 超长）ChatSocket 那边有 WARN 日志。
        markMessageState(m_active_uid, message_id, QStringLiteral("failed"));
        m_sending.remove(msg_id);

        setTip(QStringLiteral("消息发送失败：未连接到聊天服务器"), true);
    }

    return true;
}

void ChatController::onTextMessageResult(const bool ok, const int error, const QString& msgId,
                                         const QString& toUid, const QString& body)
{
    Q_UNUSED(body)

    /*
     * 按 msg_id 认领：1014 回显的就是它。
     * 找不到只有两种可能 —— 迟到的回执（超时/断线时已经标红了），
     * 或者这条根本不是本类发的。两种都不该动界面，留一条警告即可。
     */
    auto it = m_sending.find(msgId);
    if (it == m_sending.end())
    {
        qWarning() << "[chat] 收到无法认领的消息回执, msg_id =" << msgId
            << "to_uid =" << toUid << "error =" << error;
        return;
    }

    const PendingSend entry = it.value();
    m_sending.erase(it);

    if (entry.peer_uid != toUid)
    {
        // 只有一种可能：服务端把 to_uid 回错 / 我们自己记错了。仍按本地记录标状态。
        qWarning() << "[chat] 消息回执的 to_uid 与发送记录不一致: 记录"
            << entry.peer_uid << "回包" << toUid;
    }

    markMessageState(entry.peer_uid, entry.message_id,
                     ok ? QStringLiteral("sent") : QStringLiteral("failed"));

    if (ok)
    {
        // error == 0 只代表"已写入对方离线消息流"，不代表对端已收到 —— 文案别写成"已读"
        setTip(QString());
        return;
    }

    qWarning() << "[chat] 发送失败, to_uid =" << toUid << "msg_id =" << msgId
        << "error =" << error;
    setTip(QStringLiteral("消息发送失败：%1").arg(mychat::protocol::errorToTip(error)), true);
}

void ChatController::appendIncoming(const QString& peerUid, const QString& text, const QString& time)
{
    if (peerUid.isEmpty() || text.isEmpty())
        return;

    const QString stamp = time.isEmpty() ? nowText() : time;

    // 会话不存在说明是个新联系人：先建一行，否则消息没地方落
    if (indexOf(peerUid) < 0)
    {
        QVariantMap item;
        item.insert(QStringLiteral("uid"), peerUid);
        item.insert(QStringLiteral("name"), peerUid);
        item.insert(QStringLiteral("nick"), QString());
        item.insert(QStringLiteral("displayName"), peerUid);
        item.insert(QStringLiteral("icon"), QString());
        item.insert(QStringLiteral("desc"), QString());
        item.insert(QStringLiteral("sex"), 2);
        item.insert(QStringLiteral("lastText"), QString());
        item.insert(QStringLiteral("lastTime"), QString());
        item.insert(QStringLiteral("unread"), 0);
        item.insert(QStringLiteral("demo"), false);
        m_conversations.prepend(item);
    }

    appendMessage(peerUid, false, text, stamp, QStringLiteral("sent"));
    touchConversation(peerUid, text, stamp);

    // 当前正开着这个会话 → 直接可视，不该记未读
    if (peerUid == m_active_uid)
    {
        markRead(peerUid);
        emit messagesChanged();
    }
    else
    {
        const int index = indexOf(peerUid);
        if (index >= 0)
        {
            QVariantMap item = m_conversations.at(index).toMap();
            item.insert(QStringLiteral("unread"),
                        item.value(QStringLiteral("unread")).toInt() + 1);
            m_conversations[index] = item;
        }
    }

    emit conversationsChanged();
}

void ChatController::markMessageState(const QString& peerUid, const int messageId,
                                      const QString& state)
{
    auto it = m_history.find(peerUid);
    if (it == m_history.end())
        return;

    QVariantList& list = it.value();
    for (int i = 0; i < list.size(); ++i)
    {
        QVariantMap item = list.at(i).toMap();
        if (item.value(QStringLiteral("id")).toInt() != messageId)
            continue;

        item.insert(QStringLiteral("state"), state);
        list[i] = item;

        if (peerUid == m_active_uid)
            emit messagesChanged();
        return;
    }
}

int ChatController::appendMessage(const QString& peerUid, const bool from_self,
                                  const QString& text, const QString& time,
                                  const QString& state)
{
    const int id = m_next_message_id++;
    m_history[peerUid].append(makeMessage(id, from_self, text, time, state));
    return id;
}

void ChatController::touchConversation(const QString& peerUid, const QString& last_text,
                                       const QString& last_time)
{
    const int index = indexOf(peerUid);
    if (index < 0)
        return;

    QVariantMap item = m_conversations.at(index).toMap();
    item.insert(QStringLiteral("lastText"), last_text);
    item.insert(QStringLiteral("lastTime"), last_time);

    // 有新消息就置顶（服务端排序不可靠时，这一条也要保持）
    m_conversations.removeAt(index);
    m_conversations.prepend(item);
}

void ChatController::setTip(const QString& text, const bool is_error)
{
    if (m_tip == text && m_tip_is_error == is_error)
        return;

    m_tip = text;
    m_tip_is_error = is_error;
    emit tipChanged();
    emit tipIsErrorChanged();
}
