#include "ChatController.h"

#include <QDateTime>
#include <QTime>
#include <QTimer>
#include <QVariantMap>
#include <QtGlobal>

#include <algorithm>
#include <utility>

#include "ChatSocket.h"
#include "SessionManager.h"
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

/**
 * 收到 1022 后等多久再拉（毫秒）。
 *
 * 文档 §3.1 的建议是 2s：一条一条拉太浪费，等一小会儿正好把连续几条合并成一批。
 * 攒够 10 条就不等了，直接拉（见 messageReceived 的接线）。
 */
constexpr int kPullDebounceMs = 2000;

/// 拉满一批（10 条）后，隔多久再拉下一批 —— 说明可能还有积压，但别空转
constexpr int kPullBacklogDelayMs = 200;

/// 拉取失败（超时 / 服务端错误）后隔多久重试：消息还在服务端，重试是安全的
constexpr int kPullRetryDelayMs = 2000;

/**
 * 兜底拉取周期（毫秒）。
 *
 * 正常链路是「1022 推送 → 2s 合并 → 1023 拉取」。但推送本身可能静默失效
 * （例如 2026-10-23 修掉的服务端竞态：旧连接断开误删新会话映射，在线状态还在、
 * 1022 却被丢掉），这时客户端不主动拉就永远不知道有新消息。
 *
 * 登录后每 30s 主动发一次 1023，保证最坏情况下也只延迟一个周期；
 * 空闲时只是一次空请求，代价可忽略。断线时停止，重登再启动。
 */
constexpr int kFallbackPullIntervalMs = 30 * 1000;

/// 自检用：把兜底周期改短，别让测试真的等 30s（未设置 / 非正数 = 用默认值）
constexpr const char* kFallbackPullEnv = "MYCHAT_FALLBACK_PULL_MS";

/// 确认失败后隔多久再拉一次（PEL 会把那批消息重投回来，去重后重新确认）
constexpr int kAckRetryDelayMs = 1000;
/// 确认连续失败这么多次就不再自动重试（避免对着一个不健康的服务端空转）
constexpr int kAckRetryLimit = 3;

/// 去重表上限（条）：一个会话窗口的量级，够用且不会无限长
constexpr int kSeenMessageLimit = 2000;

/// 每个会话第一次打开时从本地库读多少条历史（再往前的靠"往上翻页"按需读）
constexpr int kHistoryMessageLimit = 200;

/// 往上翻页一次读多少条
constexpr int kHistoryPageSize = 20;

/// 一条本地库消息 → QML 的 messages 项（首次加载和翻页共用，别各写一份）
QVariantMap historyItem(const ChatStore::Message& row)
{
    QVariantMap item;
    item.insert(QStringLiteral("id"), row.id);
    item.insert(QStringLiteral("fromSelf"), row.fromSelf);
    item.insert(QStringLiteral("text"), row.body);
    item.insert(QStringLiteral("time"),
                QDateTime::fromMSecsSinceEpoch(row.createdMs).toString(QStringLiteral("HH:mm")));
    item.insert(QStringLiteral("state"), row.state);
    // 毫秒时间戳：QML 靠它算"两条消息隔了多久"，决定要不要插一条居中的时间标签
    item.insert(QStringLiteral("ts"), row.createdMs);
    /*
     * 发送者 uid：群聊里每条消息的发送者都不同，气泡上的名字 / 头像要按它取
     * （单聊里它等于对方 uid，界面照旧用会话名，不冲突）。
     * 以前这里把它丢掉了 —— 单聊看不出问题，群聊就没法分辨谁说的。
     */
    item.insert(QStringLiteral("senderUid"), row.fromUid);
    /*
     * 群操作系统消息：服务端没有独立类型，只把 from_uid 置为保留的 "1"
     * （api-group.md §9.5）。历史消息重启后读回来也要按系统消息渲染，
     * 所以标志在生成消息项时就定下来，不能只依赖收消息那一刻的判断。
     */
    const bool system = !row.fromSelf
        && row.peerUid.startsWith(QLatin1Char('g'))
        && row.fromUid == QLatin1String("1");
    item.insert(QStringLiteral("system"), system);
    return item;
}

QString nowText()
{
    return QTime::currentTime().toString(QStringLiteral("HH:mm"));
}

/**
 * 从 Redis Stream ID（`<毫秒时间戳>-<序号>`）里取一个 HH:mm 显示时间。
 *
 * 服务端不回消息时间，但消息编号本身就是毫秒时间戳 —— 比"拉到的时间"更准
 * （离线消息可能是几天前的）。解析不出来就退回当前时间。
 */
/**
 * Stream ID（`<毫秒>-<序号>`）→ 毫秒时间戳。
 *
 * @return 0 = 解析失败（调用方用当前时间兜底）
 */
qint64 streamIdToMsecs(const QString& id)
{
    const qsizetype dash = id.indexOf(QLatin1Char('-'));
    if (dash <= 0)
        return 0;

    bool be_valid = false;
    const qint64 msecs = id.left(dash).toLongLong(&be_valid);
    return be_valid && msecs > 0 ? msecs : 0;
}

QString timeFromStreamId(const QString& id)
{
    const qint64 msecs = streamIdToMsecs(id);
    if (msecs <= 0)
        return nowText();

    return QDateTime::fromMSecsSinceEpoch(msecs).toString(QStringLiteral("HH:mm"));
}

QVariantMap makeMessage(const int id, const bool from_self, const QString& text,
                        const QString& time, const QString& state,
                        const qint64 created_ms = 0, const QString& sender_uid = QString())
{
    QVariantMap item;
    item.insert(QStringLiteral("id"), id);
    item.insert(QStringLiteral("fromSelf"), from_self);
    item.insert(QStringLiteral("text"), text);
    item.insert(QStringLiteral("time"), time);
    item.insert(QStringLiteral("state"), state);
    // 毫秒时间戳（QML 用来决定"要不要插时间标签"）；演示数据没有真实时间，给 0 表示未知
    item.insert(QStringLiteral("ts"), created_ms);
    /// 群聊气泡按它取发送者名字（见 historyItem 的说明）
    item.insert(QStringLiteral("senderUid"), sender_uid);
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

    // 1013 的结果：按 msg_id 认领（1014 会原样回显它）
    connect(&socket, &mychat::ChatSocket::textMessageResult,
            this, &ChatController::onTextMessageResult);

    // 1024 的结果：落消息 + 确认 + 决定要不要继续拉
    connect(&socket, &mychat::ChatSocket::chatMessagesReceived,
            this, &ChatController::onChatMessages);

    /*
     * 1022 推送只说明"有新消息"，内容要自己拉（1023）。
     *
     * 节流规则见 api-chat.md §3.1：攒够 10 条立刻拉，否则 2s 后拉。
     * 计时器**不**因为每条推送重启 —— 持续有消息时那样会一直推后，永远拉不成。
     */
    m_pull_timer = new QTimer(this);
    m_pull_timer->setSingleShot(true);
    connect(m_pull_timer, &QTimer::timeout, this, &ChatController::requestPull);

    /*
     * 兜底拉取：登录后每 30s 主动拉一次，不依赖 1022。
     *
     * 推送链路可能静默失效（1022 丢了 / 服务端在线状态与会话映射不一致等），
     * 只靠"收到推送才拉"就会出现"能发不能收、重登才恢复"。有了这层兜底，
     * 最坏情况下也只晚一个周期看到消息。断线时停止，重登时再启动。
     */
    m_fallback_pull_timer = new QTimer(this);
    const int fallback_interval = qEnvironmentVariableIntValue(kFallbackPullEnv);
    m_fallback_pull_timer->setInterval(fallback_interval > 0
                                           ? fallback_interval
                                           : kFallbackPullIntervalMs);
    connect(m_fallback_pull_timer, &QTimer::timeout, this, &ChatController::requestPull);

    connect(&socket, &mychat::ChatSocket::messageReceived, this, [this]()
                {
                    ++m_pull_hint;

                    if (m_pull_hint > mychat::protocol::MAX_CHAT_MESSAGES_PER_BATCH)
                    {
                        requestPull();
                        return;
                    }

                    if (!m_pull_timer->isActive())
                        m_pull_timer->start(kPullDebounceMs);
                });

    /*
     * 登录成功后主动拉一次：离线期间的消息**不会**补推 1022，
     * 不主动拉就得等下一条消息来才顺带看到（甚至永远看不到）。
     * 顺便把去重表清空 —— 新会话、新进程，PEL 里没确认的消息本来就该重新显示。
     */
    connect(&socket, &mychat::ChatSocket::loginSucceeded, this,
            [this](const std::uint64_t uid, const QString&, const QString&)
                {
                    m_pull_hint = 0;
                    m_pull_again = false;
                    m_pull_count = 0;
                    m_ack_retry = 0;
                    m_seen_message_ids.clear();
                    m_seen_message_order.clear();
                    // "本地已移除的群"只对本次登录有效：换账号后同一个 gid
                    // 可能是另一个人的群，不能继续挡系统消息。
                    m_removed_gids.clear();

                    // 自己的 uid：群消息要判断"这条是不是我自己发的回显"。
                    // 存这里而不是每次读 SessionManager —— 测试里没有 LoginController
                    // 去 update 它，登录后的 uid 只在这一帧里可靠。
                    m_self_uid = QString::number(uid);

                    // 先开本地库：登录后马上要拉的离线消息要能落盘（落库后才 ACK）
                    openStore(uid);
                    requestPull();

                    // 登录成功才开始兜底拉取：未登录时 requestPull 也会直接返回，
                    // 但没必要让定时器空转（自检里会断言"未登录不发 1023"）。
                    m_fallback_pull_timer->start();
                });

    /*
     * 确认失败不必慌：消息还在服务端 PEL 里，会随下一轮拉取重投回来。
     * 隔一秒再拉一次（去重会挡掉重复气泡），连续失败几次就停手。
     */
    connect(&socket, &mychat::ChatSocket::chatMessagesAcked, this,
            [this](const bool ok, const int error)
                {
                    if (ok)
                    {
                        m_ack_retry = 0;
                        return;
                    }

                    qWarning() << "[chat] 确认消息失败, error =" << error
                        << "，准备重新拉取（PEL 会重投）";

                    if (m_ack_retry >= kAckRetryLimit)
                        return;

                    ++m_ack_retry;
                    QTimer::singleShot(kAckRetryDelayMs, this, &ChatController::requestPull);
                });

    /*
     * 断线：在飞的消息不会再有回执（ChatSocket 断线时直接清空队列，不补报超时），
     * 所以必须在这里把它们标红并销账，否则界面上会永远停在「发送中」。
     * 拉取节流状态也一起复位，重连（重新登录）后从 0 开始。
     */
    connect(&socket, &mychat::ChatSocket::linkError, this, [this](const QString&)
                {
                    if (m_pull_timer != nullptr)
                        m_pull_timer->stop();
                    // 断线后 requestPull 本来就会早退，停掉定时器只是别空转；
                    // 重新登录（loginSucceeded）会再启动。
                    if (m_fallback_pull_timer != nullptr)
                        m_fallback_pull_timer->stop();
                    m_pull_hint = 0;
                    m_pull_again = false;
                    m_pull_count = 0;

                    if (m_sending.isEmpty())
                        return;

                    for (const PendingSend& entry: std::as_const(m_sending))
                        markMessageState(entry.peer_uid, entry.message_id, QStringLiteral("failed"));

                    m_sending.clear();
                    setTip(QStringLiteral("连接已断开，发送中的消息未送达"), true);
                });
}

void ChatController::openStore(const std::uint64_t uid)
{
    m_store.close();

    /*
     * 换账号 = 换一整套会话数据，必须把**账号相关的内存状态**全部丢掉。
     *
     * 只关库句柄是不够的（以前就是这样）：退出 1001 再登录 1002 时，
     *   · m_conversations 里还留着 1001 的会话投影。其中 peer_uid == 1002 的那条，
     *     在新账号视角下就成了"和自己聊天"，摘要还是上一轮的聊天记录 ——
     *     看起来就像"先前账号的记录被复制了一份"；
     *   · m_history / m_history_loaded 不清，旧账号的消息会串进新账号的会话；
     *   · m_active_uid 还指着上一个账号的对话。
     *
     * demo 数据（MYCHAT_DEMO=1 注入的版式自检数据）不属于任何账号，原样保留。
     */
    QSet<QString> demo_uids;
    QVariantList demo_conversations;
    for (const QVariant& entry: std::as_const(m_conversations))
    {
        const QVariantMap item = entry.toMap();
        if (!item.value(QStringLiteral("demo")).toBool())
            continue;
        demo_conversations.append(entry);
        demo_uids.insert(item.value(QStringLiteral("uid")).toString());
    }

    const bool had_state = !m_conversations.isEmpty() || !m_history.isEmpty()
                           || !m_active_uid.isEmpty();

    m_conversations = demo_conversations;
    for (auto it = m_history.begin(); it != m_history.end();)
    {
        if (demo_uids.contains(it.key()))
            ++it;
        else
            it = m_history.erase(it);
    }

    m_history_loaded.clear();
    m_history_has_more.clear();
    m_history_loading = false;
    m_active_uid.clear();
    m_active_name.clear();
    m_active_is_demo = false;
    m_active_is_group = false;
    m_active_gid.clear();
    m_sending.clear();

    if (uid == 0 || !m_store.open(uid))
    {
        // 常见原因：windeployqt 没带 sqldrivers/qsqlite.dll，或者目录没权限。
        // 不阻断聊天 —— 只是这次会话不会留下历史。
        qWarning() << "[chat] 本地记录不可用，本次聊天不会保存";
        setTip(QStringLiteral("本地记录不可用，本次聊天不会保存"));
        if (had_state)
        {
            emit conversationsChanged();
            emit messagesChanged();
            emit activeConversationChanged();
        }
        return;
    }

    /*
     * 回填会话列表：库里存的只是摘要（最后一条 / 未读），显示名等好友列表同步时补。
     * 库按 updated_ms 倒序，这里按同样顺序 append 即可。
     *
     * ⚠ 群会话（kind = group）的键在库里就是 `g<gid>`，所以能原样回填；
     *   群名来自 groups 表 —— 服务端没有群资料查询接口，这是唯一的来源。
     */
    QHash<QString, ChatStore::Group> groups;
    for (const ChatStore::Group& group: m_store.groups())
        groups.insert(group.gid, group);

    const QList<ChatStore::Conversation> rows = m_store.conversations();
    for (const ChatStore::Conversation& row: rows)
    {
        const bool is_group = row.kind == QLatin1String("group");
        const QString gid = is_group ? groupIdOfKey(row.peerUid) : QString();
        const ChatStore::Group group = groups.value(gid);

        // 群名：本地有就用；没有（换设备 / 清过库）就显示「群 <gid>」，别空着
        const QString fallback_name = is_group
                                          ? (!group.name.isEmpty()
                                                 ? group.name
                                                 : QStringLiteral("群 %1").arg(gid))
                                          : row.peerUid;

        const int index = indexOf(row.peerUid);
        if (index >= 0)
        {
            QVariantMap item = m_conversations.at(index).toMap();
            item.insert(QStringLiteral("lastText"), row.lastText);
            item.insert(QStringLiteral("lastTime"), row.lastTime);
            item.insert(QStringLiteral("unread"), row.unread);
            if (is_group)
            {
                // 好友同步不会碰群会话，名字 / 角色这里补一次
                item.insert(QStringLiteral("kind"), QStringLiteral("group"));
                item.insert(QStringLiteral("gid"), gid);
                item.insert(QStringLiteral("name"), fallback_name);
                item.insert(QStringLiteral("displayName"), fallback_name);
                item.insert(QStringLiteral("icon"), group.icon);
            }
            m_conversations[index] = item;
            continue;
        }

        // 本地有记录、但当前不在好友列表里（删过好友 / 陌生人发过消息 / 群聊）
        QVariantMap item;
        item.insert(QStringLiteral("uid"), row.peerUid);
        item.insert(QStringLiteral("name"), fallback_name);
        item.insert(QStringLiteral("nick"), QString());
        item.insert(QStringLiteral("displayName"), fallback_name);
        item.insert(QStringLiteral("icon"), is_group ? group.icon : QString());
        item.insert(QStringLiteral("desc"), QString());
        item.insert(QStringLiteral("sex"), 2);
        item.insert(QStringLiteral("kind"), is_group ? QStringLiteral("group")
                                                    : QStringLiteral("user"));
        item.insert(QStringLiteral("gid"), gid);
        item.insert(QStringLiteral("lastText"), row.lastText);
        item.insert(QStringLiteral("lastTime"), row.lastTime);
        item.insert(QStringLiteral("unread"), row.unread);
        item.insert(QStringLiteral("demo"), false);
        m_conversations.append(item);
    }

    if (!rows.isEmpty())
    {
        qInfo() << "[chat] 从本地库回填" << rows.size() << "个会话";
        emit conversationsChanged();
    }
    else if (had_state)
    {
        // 新账号没有本地记录，但内存里还有上一个账号（或 demo）的列表 → 也要刷一次
        emit conversationsChanged();
    }

    if (had_state)
    {
        // 活动会话已经清空，QML 那边的 onConversationsChanged 会重新打开第一个
        emit messagesChanged();
        emit activeConversationChanged();
    }
}

void ChatController::ensureHistoryLoaded(const QString& peerUid)
{
    if (peerUid.isEmpty() || m_history_loaded.contains(peerUid))
        return;

    // 先标记再查：空历史也算"加载过"，否则每次点开都会查一遍库
    m_history_loaded.insert(peerUid);

    if (!m_store.isOpen())
    {
        // 没有本地库就没有"更早的历史"可翻；已经加载过的旧值也要收掉
        if (m_history_has_more.remove(peerUid))
            emit historyChanged();
        return;
    }

    const QList<ChatStore::Message> rows = m_store.recentMessages(peerUid, kHistoryMessageLimit);

    // 读满了整整一页 → 前面多半还有；没读满就是到头了
    m_history_has_more[peerUid] = (rows.size() >= kHistoryMessageLimit);
    if (rows.isEmpty())
    {
        emit historyChanged();
        return;
    }

    QVariantList list;
    list.reserve(rows.size());
    for (const ChatStore::Message& row: rows)
    {
        QVariantMap item = historyItem(row);
        item.insert(QStringLiteral("senderName"), senderNameFor(row.fromUid));
        list.append(item);
    }

    m_history[peerUid] = list;
    qInfo() << "[chat] 从本地库加载" << list.size() << "条历史:" << peerUid;
    emit historyChanged();
}

int ChatController::loadOlderMessages(const int count)
{
    if (m_active_uid.isEmpty() || m_history_loading || count <= 0)
        return 0;
    if (!historyHasMore() || !m_store.isOpen())
        return 0;

    const QString peer = m_active_uid;
    const QVariantList current = m_history.value(peer);
    if (current.isEmpty())
    {
        m_history_has_more[peer] = false;
        emit historyChanged();
        return 0;
    }

    // 当前最早的一条就是游标：比它的 id 更小 = 更早
    const qint64 oldest_id = current.first().toMap().value(QStringLiteral("id")).toLongLong();
    if (oldest_id <= 0)
        return 0;

    m_history_loading = true;
    emit historyChanged();

    const QList<ChatStore::Message> rows = m_store.messagesBefore(peer, oldest_id, count);
    m_history_loading = false;

    if (rows.isEmpty())
    {
        m_history_has_more[peer] = false;
        emit historyChanged();
        return 0;
    }

    QVariantList merged;
    merged.reserve(rows.size() + current.size());
    for (const ChatStore::Message& row: rows)
    {
        QVariantMap item = historyItem(row);
        item.insert(QStringLiteral("senderName"), senderNameFor(row.fromUid));
        merged.append(item);
    }
    merged.append(current);

    m_history[peer] = merged;
    m_history_has_more[peer] = (rows.size() >= count);

    qInfo() << "[chat] 往上翻页" << rows.size() << "条 → 共" << merged.size() << "条:" << peer;

    emit messagesChanged();
    emit historyChanged();
    return static_cast<int>(rows.size());
}

int ChatController::deleteMessages(const QVariantList& ids)
{
    QList<qint64> row_ids;
    row_ids.reserve(ids.size());
    for (const QVariant& value: ids)
    {
        bool be_valid = false;
        const qint64 id = value.toLongLong(&be_valid);
        if (be_valid && id > 0)
            row_ids.append(id);
    }

    if (row_ids.isEmpty())
        return 0;

    const int removed = m_store.deleteMessages(row_ids);
    if (removed < 0)
    {
        setTip(QStringLiteral("删除失败：本地记录写入异常"), true);
        return 0;
    }

    // 内存里同步删（id 是全局唯一的，虽然通常只会命中当前会话）
    QSet<qint64> id_set;
    for (const qint64 id: row_ids)
        id_set.insert(id);

    QSet<QString> touched;
    for (auto it = m_history.begin(); it != m_history.end(); ++it)
    {
        QVariantList& list = it.value();
        for (int i = list.size() - 1; i >= 0; --i)
        {
            if (!id_set.contains(list.at(i).toMap().value(QStringLiteral("id")).toLongLong()))
                continue;

            list.removeAt(i);
            touched.insert(it.key());
        }
    }

    // 摘要按"库里剩下的最后一条"重算，否则删掉最后一条后列表上还挂着那句
    for (const QString& peer: touched)
        refreshConversationSummary(peer);

    if (touched.contains(m_active_uid))
        emit messagesChanged();
    if (!touched.isEmpty())
        emit conversationsChanged();

    setTip(QStringLiteral("已删除 %1 条聊天记录（仅本机）").arg(removed));
    return removed;
}

void ChatController::clearConversation(const QString& peerUid)
{
    if (peerUid.isEmpty())
        return;

    if (m_store.isOpen() && !m_store.clearConversation(peerUid))
        qWarning() << "[chat] 清空本地记录失败:" << peerUid;

    m_history[peerUid].clear();
    // 标成"已加载"：库里已经空了，别让懒加载再查一遍
    m_history_loaded.insert(peerUid);
    // 库都清空了，自然没有"更早的历史"可翻
    m_history_has_more.remove(peerUid);

    const int index = indexOf(peerUid);
    QString name = peerUid;
    if (index >= 0)
    {
        QVariantMap item = m_conversations.at(index).toMap();
        name = item.value(QStringLiteral("displayName")).toString();
        item.insert(QStringLiteral("lastText"), QString());
        item.insert(QStringLiteral("lastTime"), QString());
        item.insert(QStringLiteral("unread"), 0);
        m_conversations[index] = item;
        /*
         * 这里**故意不回写会话摘要**：清空时 store 已经把 conversations 行删了，
         * 再 persist 会立刻造回一条"空摘要"的行。等真有新消息时 touchConversation
         * 会重新建行；期间重启，好友会话由好友列表重建、陌生人会话自然消失。
         */
    }

    if (peerUid == m_active_uid)
    {
        emit messagesChanged();
        emit historyChanged();
    }
    emit conversationsChanged();

    setTip(QStringLiteral("已清空与 %1 的聊天记录（仅本机，对方不受影响）")
               .arg(name.isEmpty() ? peerUid : name));
}

void ChatController::refreshConversationSummary(const QString& peerUid)
{
    const int index = indexOf(peerUid);
    if (index < 0)
        return;

    ChatStore::Message last;
    const bool has_last = m_store.lastMessage(peerUid, last);

    QVariantMap item = m_conversations.at(index).toMap();
    item.insert(QStringLiteral("lastText"), has_last ? last.body : QString());
    item.insert(QStringLiteral("lastTime"),
                has_last
                    ? QDateTime::fromMSecsSinceEpoch(last.createdMs).toString(QStringLiteral("HH:mm"))
                    : QString());
    if (!has_last)
        item.insert(QStringLiteral("unread"), 0); // 删空了，红点也没意义
    m_conversations[index] = item;

    persistConversation(peerUid);
}

void ChatController::persistConversation(const QString& peerUid)
{
    if (!m_store.isOpen())
        return;

    const int index = indexOf(peerUid);
    if (index < 0)
        return;

    const QVariantMap item = m_conversations.at(index).toMap();

    ChatStore::Conversation row;
    row.peerUid = peerUid;
    row.lastText = item.value(QStringLiteral("lastText")).toString();
    row.lastTime = item.value(QStringLiteral("lastTime")).toString();
    row.unread = item.value(QStringLiteral("unread")).toInt();
    row.updatedMs = QDateTime::currentMSecsSinceEpoch();
    // 群会话的键是 g<gid>，重启后要靠 kind 才能把它还原成群 —— 必须一起写
    row.kind = item.value(QStringLiteral("kind")).toString();
    if (row.kind.isEmpty())
        row.kind = QStringLiteral("user");
    m_store.saveConversation(row);
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

    /*
     * 群键（`g<gid>`）但会话表里还没有这一条：先补一个占位会话。
     *
     * 两种来源：自动化先打开、以及"消息还在路上"。不补的话下面算出来的
     * kind 是空 → m_active_is_group = false → 发消息会按**单聊**发到群号上，
     * 服务端回 1022，用户看到的是"你们还不是好友"。
     */
    if (isGroupKey(peerUid) && indexOf(peerUid) < 0)
    {
        const QString gid = groupIdOfKey(peerUid);
        // 调用方可能把会话键 / 群号当名字传进来（自动化就是这么干的），
        // 那不是群名 —— 交给 ensureGroupConversation 用「群 <gid>」兜底
        const QString hint = (peerName == peerUid || peerName == gid) ? QString() : peerName;
        ensureGroupConversation(gid, hint);
    }

    const bool changed = (peerUid != m_active_uid);
    m_active_uid = peerUid;
    m_active_name = peerName;

    // DEMO 标签要跟着会话走：点开演示会话才挂标，点开真实好友不该挂
    const int index = indexOf(peerUid);
    m_active_is_demo = index >= 0 && m_conversations.at(index).toMap()
                                                    .value(QStringLiteral("demo"))
                                                    .toBool();

    /*
     * 群会话：键是 `g<gid>`，界面上的群号要去掉前缀。
     * 这里不信任调用方传进来的 name —— QML 传的可能是旧名字（会话刚被改名），
     * 以会话表里的为准。
     */
    m_active_is_group = index >= 0 && m_conversations.at(index).toMap()
                                                .value(QStringLiteral("kind"))
                                                .toString() == QLatin1String("group");
    m_active_gid = m_active_is_group ? groupIdOfKey(peerUid) : QString();
    if (m_active_is_group && index >= 0)
    {
        const QString display = m_conversations.at(index).toMap()
                                    .value(QStringLiteral("displayName")).toString();
        if (!display.isEmpty())
            m_active_name = display;
    }

    // 历史在本地库里，第一次点开时懒加载（演示会话没有库，直接跳过）
    ensureHistoryLoaded(peerUid);

    markRead(peerUid);

    if (changed)
        emit activeConversationChanged();
    // historyHasMore / historyLoading 是"跟着会话走"的，换会话就得让 QML 重新读
    emit historyChanged();
    emit messagesChanged();
}

void ChatController::syncFriends(const QVariantList& friends)
{
    /*
     * 会话列表以好友列表为**主**，但不再是"好友列表的投影"：
     *
     *   · 已有会话的未读 / 摘要 / **顺序**是会话状态，重建会把它们清掉；
     *   · **非好友会话不能一概删掉** —— 服务端不校验好友关系，陌生人也能发消息，
     *     删了就等于"消息收到了但永远看不到"（本地库里有记录、界面上却没有）；
     *   · 好友被删且从没聊过的空会话才丢掉（那种留着只是噪声）；
     *   · demo = true 的演示会话原样保留（MYCHAT_DEMO=1 才有的版式自检数据）。
     *
     * 做法：先按现有顺序原地更新（好友补资料字段），再把新好友补到末尾。
     */
    QSet<QString> friend_uids;
    QHash<QString, QVariantMap> friend_fields;
    /*
     * 好友名缓存：群聊气泡的发送者名字靠它（服务端 1042 只给群昵称和 uid，
     * 没有账号昵称）。每次同步整体重建 —— 好友删了名字也该消失。
     */
    m_friend_names.clear();
    for (const QVariant& entry: friends)
    {
        const QVariantMap f = entry.toMap();
        const QString uid = f.value(QStringLiteral("uid")).toString();
        if (uid.isEmpty())
            continue;

        friend_uids.insert(uid);
        friend_fields.insert(uid, f);

        const QString display = f.value(QStringLiteral("displayName")).toString();
        const QString name = f.value(QStringLiteral("name")).toString();
        m_friend_names.insert(uid, display.isEmpty() ? name : display);
    }

    QVariantList next;
    next.reserve(m_conversations.size() + friends.size());
    QSet<QString> kept;

    for (const QVariant& entry: m_conversations)
    {
        QVariantMap conv = entry.toMap();
        const QString uid = conv.value(QStringLiteral("uid")).toString();
        if (uid.isEmpty())
            continue;

        const bool is_demo = conv.value(QStringLiteral("demo")).toBool();
        const bool is_friend = friend_uids.contains(uid);
        /*
         * 群会话：**永远保留**。
         *
         * 群不在好友列表里（那是另一个域），而且刚建好、还没人说话的群摘要就是空的 ——
         * 按下面那条"非好友 + 无摘要 = 空壳"的规则会被直接删掉，
         * 表现就是"建完群，列表里什么都没有"。
         */
        const bool is_group = conv.value(QStringLiteral("kind")).toString()
                              == QLatin1String("group");

        // 既不是好友、也不是群、也没有任何聊天记录 → 只可能是"删掉的好友留下的空壳"
        if (!is_demo && !is_group && !is_friend
            && conv.value(QStringLiteral("lastText")).toString().isEmpty())
            continue;

        if (is_friend && !is_group)
        {
            const QVariantMap f = friend_fields.value(uid);
            // ⚠ 一定要给默认值：QVariantMap 里插一个无效 QVariant，QML 那边读到的就是
            //   undefined → ConversationRow 的 QString 属性会刷 "Unable to assign [undefined]"
            conv.insert(QStringLiteral("name"), f.value(QStringLiteral("name"), QString()));
            conv.insert(QStringLiteral("nick"), f.value(QStringLiteral("nick"), QString()));
            conv.insert(QStringLiteral("displayName"),
                        f.value(QStringLiteral("displayName"), QString()));
            conv.insert(QStringLiteral("icon"), f.value(QStringLiteral("icon"), QString()));
            conv.insert(QStringLiteral("desc"), f.value(QStringLiteral("desc"), QString()));
            conv.insert(QStringLiteral("sex"), f.value(QStringLiteral("sex"), 2));
            conv.insert(QStringLiteral("demo"), false);
        }

        next.append(conv);
        kept.insert(uid);
    }

    // 新好友（本地还没有会话）补到末尾
    for (const QVariant& entry: friends)
    {
        const QVariantMap f = entry.toMap();
        const QString uid = f.value(QStringLiteral("uid")).toString();
        if (uid.isEmpty() || kept.contains(uid))
            continue;

        QVariantMap conv;
        conv.insert(QStringLiteral("uid"), uid);
        conv.insert(QStringLiteral("name"), f.value(QStringLiteral("name"), QString()));
        conv.insert(QStringLiteral("nick"), f.value(QStringLiteral("nick"), QString()));
        conv.insert(QStringLiteral("displayName"), f.value(QStringLiteral("displayName"), QString()));
        conv.insert(QStringLiteral("icon"), f.value(QStringLiteral("icon"), QString()));
        conv.insert(QStringLiteral("desc"), f.value(QStringLiteral("desc"), QString()));
        conv.insert(QStringLiteral("sex"), f.value(QStringLiteral("sex"), 2));
        conv.insert(QStringLiteral("lastText"), QString());
        conv.insert(QStringLiteral("lastTime"), QString());
        conv.insert(QStringLiteral("unread"), 0);
        conv.insert(QStringLiteral("demo"), false);
        conv.insert(QStringLiteral("kind"), QStringLiteral("user"));
        conv.insert(QStringLiteral("gid"), QString());
        next.append(conv);
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
            const QVariantMap active_item = m_conversations.at(index).toMap();
            m_active_name = active_item.value(QStringLiteral("displayName")).toString();
            m_active_is_demo = active_item.value(QStringLiteral("demo")).toBool();
            m_active_is_group = active_item.value(QStringLiteral("kind")).toString()
                                == QLatin1String("group");
            m_active_gid = m_active_is_group ? groupIdOfKey(m_active_uid) : QString();
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
    m_active_is_group = false;
    m_active_gid.clear();
    emit activeConversationChanged();
    emit historyChanged();
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

    // 未读也持久化，否则重启后又冒出红点
    m_store.markConversationRead(peerUid);

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

    /*
     * 群消息的 `to_uid` 是**群号**（不是成员 uid），并且要带 `group: true`。
     * 会话键是 `g<gid>`，所以这里要剥掉前缀 —— 直接把键发出去，
     * 服务端会 `SISMEMBER chatgroup_g20001`，永远查不到。
     */
    const QString target_uid = m_active_is_group ? m_active_gid : m_active_uid;

    if (!mychat::ChatSocket::instance().sendTextMessage(
            msg_id, target_uid, trimmed, mychat::protocol::ChatMessageType::Text,
            m_active_is_group))
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

    /*
     * 群会话的键是 `g<gid>`，而回执里的 to_uid 是**群号本身** —— 比较前先剥前缀，
     * 否则每条群消息都会刷一条"回执的 to_uid 与发送记录不一致"的假警告。
     */
    const bool is_group = isGroupKey(entry.peer_uid);
    const QString expected_uid = is_group ? groupIdOfKey(entry.peer_uid) : entry.peer_uid;
    if (expected_uid != toUid)
    {
        // 只有一种可能：服务端把 to_uid 回错 / 我们自己记错了。仍按本地记录标状态。
        qWarning() << "[chat] 消息回执的 to_uid 与发送记录不一致: 记录"
            << expected_uid << "回包" << toUid;
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

    if (error == static_cast<int>(mychat::protocol::ErrorCode::NotFriend))
    {
        if (is_group)
        {
            /*
             * 群消息的 1022 是"我不在群里"（服务端复用 NOT_FRIEND，见 api-group.md §9.1）：
             *   · 群被解散了（Redis 集合被 DEL，但我这边没收到任何通知）；
             *   · 或者我被移出了群。
             * 两种都没法从协议上区分，也不该顺手刷好友列表 —— 那条链路救不了群。
             */
            setTip(QStringLiteral("发送失败：你已不在这个群里（群可能已解散）"), true);
            return;
        }

        /*
         * 1022 = 服务端的好友校验没过。除了"本来就不是好友"，最常见的原因是**对方把我删了**
         * （服务端不推送这种变化），所以顺手让界面静默刷新一次好友列表。
         */
        emit peerIsNotFriend(toUid);
    }

    setTip(QStringLiteral("消息发送失败：%1").arg(mychat::protocol::errorToTip(error)), true);
}

void ChatController::requestPull()
{
    m_pull_timer->stop();

    auto& socket = mychat::ChatSocket::instance();
    if (!socket.isLoggedIn())
    {
        /*
         * 主动退出登录时 socket.reset() 不会发 linkError（它先把 m_logged_in
         * 清掉了），兜底定时器可能还活着。让它在下一次 tick 时自己停掉，
         * 别在登录页空转 —— 登录成功后 loginSucceeded 会重新 start。
         */
        if (m_fallback_pull_timer != nullptr)
            m_fallback_pull_timer->stop();
        return; // 登录成功后会自动拉一次，这里不用嚷嚷
    }

    /*
     * 条数 = min(待拉取计数, 10)：
     *   · 计数为 0 是"登录后主动拉"或"上批拉满了"→ 直接拉满；
     *   · 计数只有 2 就只拉 2 条，没必要多问。
     */
    const int count = m_pull_hint > 0
                          ? std::min(m_pull_hint, mychat::protocol::MAX_CHAT_MESSAGES_PER_BATCH)
                          : mychat::protocol::MAX_CHAT_MESSAGES_PER_BATCH;

    if (!socket.syncChatMessages(count))
    {
        // 已有一个拉取在飞：记下意图，等那一批回来再补。**不能丢** ——
        // 丢了就意味着"有新消息但永远不拉"，界面上什么都看不到。
        m_pull_again = true;
        return;
    }

    m_pull_hint = 0;
    // 记下这次要了几条：回来的条数 == 它，说明服务端那边可能还有（PEL 或新消息）
    m_pull_count = count;
}

bool ChatController::markMessageSeen(const QString& id)
{
    if (id.isEmpty() || m_seen_message_ids.contains(id))
        return false;

    m_seen_message_ids.insert(id);
    m_seen_message_order.enqueue(id);

    while (m_seen_message_order.size() > kSeenMessageLimit)
    {
        const QString oldest = m_seen_message_order.dequeue();
        m_seen_message_ids.remove(oldest);
    }
    return true;
}

void ChatController::setWindowActive(const bool active)
{
    if (m_window_active == active)
        return;

    m_window_active = active;
    emit windowActiveChanged();
}

void ChatController::setChatVisible(const bool visible)
{
    if (m_chat_visible == visible)
        return;

    m_chat_visible = visible;
    emit chatVisibleChanged();
}

void ChatController::setChatAtBottom(const bool at_bottom)
{
    if (m_chat_at_bottom == at_bottom)
        return;

    m_chat_at_bottom = at_bottom;
    emit chatAtBottomChanged();

    /*
     * 滚回底部 = 最新那条已经摆在眼前了，这段时间攒的未读当场清掉。
     *
     * 只在"真的在看"时才清：窗口在后台、或者人已经切到别的分区，
     * 底层列表恰好滚到底了也不算看过（attention() 会把这两种情况挡掉）。
     */
    if (m_chat_at_bottom && attention())
        markRead(m_active_uid);
}

void ChatController::onChatMessages(const QList<mychat::protocol::ChatMessage>& messages,
                                    const int error)
{
    if (messages.isEmpty())
    {
        if (error != kSuccess)
        {
            // 消息还在服务端（没被 ACK 的不丢），过两秒再试一次，比干等强
            qWarning() << "[chat] 拉取消息失败, error =" << error << "，2s 后重试";
            QTimer::singleShot(kPullRetryDelayMs, this, &ChatController::requestPull);
            return;
        }

        if (m_pull_again || m_pull_hint > 0)
        {
            m_pull_again = false;
            requestPull();
        }
        return;
    }

    /*
     * 逐条落进会话：
     *   · **按 msgs[].id 去重** —— 服务端 PEL 优先重投，同一条会被拉到多次；
     *   · 发送者没有会话就建一条（服务端不校验好友关系，理论上能收到陌生人的消息）；
     *   · 时间从 Stream ID 的毫秒部分取 —— 服务端不回消息时间，离线消息可能是几天前的；
     *   · 未读先按会话累计，最后一次性写回，避免每加一条就重建一次列表。
     */
    QStringList ids;
    ids.reserve(messages.size());

    QHash<QString, int> unread_delta;
    /// 当前会话收到了消息，且人正看着 → 当场标已读
    bool active_got_message = false;
    /// 当前会话收到了消息（不管看没看）→ 消息列表要刷一次
    bool active_has_new = false;
    int skipped = 0;

    for (const mychat::protocol::ChatMessage& message: messages)
    {
        // 重复的（PEL 重投）不落库，但**照样确认**，否则它会一直被重投
        if (!markMessageSeen(message.id))
        {
            ++skipped;
            ids.append(message.id);
            continue;
        }

        /*
         * 群消息（2026-10-23）：
         *   · `group = true` 时 `to_uid` 是群号，消息归属看 `group_id`；
         *   · 服务端会把群消息**也给发送者本人写一条**离线流（api-group.md §9.2），
         *     所以拉回来时会看到自己刚发的那条 —— 本地气泡已经在了，
         *     直接确认、不上屏，否则每条自己发的群消息都会重复显示一遍；
         *   · 升级前入队的群消息没有 `group_id`，没法归属：确认掉，留一条日志。
         */
        if (message.group && QString::number(message.fromUid) == m_self_uid)
        {
            qDebug() << "[chat] 跳过自己发送的群消息回显, id =" << message.id
                << "gid =" << message.groupId;
            ids.append(message.id);
            continue;
        }

        if (message.group && message.groupId.isEmpty())
        {
            qWarning() << "[chat] 群消息缺少 group_id（升级前入队的历史消息），已确认但不上屏:"
                << message.id;
            ids.append(message.id);
            continue;
        }

        /*
         * 本机已经主动移除的群（解散 / 被移出）：
         * 服务端解散时是「先写系统消息、再 DEL」（api-group.md §7），所以很可能
         * 在 removeGroupConversation() 之后才拉到「该群聊已解散」——
         * 不挡住的话 onChatMessages 会 ensureGroupConversation 把会话重新建出来。
         */
        if (message.group && m_removed_gids.contains(message.groupId))
        {
            qDebug() << "[chat] 群会话已在本地移除，忽略后续消息, gid =" << message.groupId;
            ids.append(message.id);
            continue;
        }

        const QString peer = message.group
                                 ? groupKey(message.groupId)
                                 : QString::number(message.fromUid);

        if (message.group)
            ensureGroupConversation(message.groupId);
        else
            ensureConversation(peer);

        const QString stamp = timeFromStreamId(message.id);

        /*
         * 收到的消息状态用 "sent"：气泡组件对 sent 不挂任何小字（正常消息）。
         * 时间 / 服务端编号一起写进库 —— `server_id` 的 UNIQUE 是跨进程去重的兜底。
         */
        const int local_id = appendMessage(peer, false, message.body, stamp,
                                           QStringLiteral("sent"),
                                           streamIdToMsecs(message.id), message.id,
                                           QString::number(message.fromUid));
        if (local_id == 0)
        {
            // 库里已经有同一个 server_id（上个进程留下的 PEL 重投）：不显示，但照样确认
            ++skipped;
            ids.append(message.id);
            continue;
        }

        touchConversation(peer, message.body, stamp);

        /*
         * 群操作的系统消息（from_uid 固定 1）：
         *   · 界面按 system 标志渲染成居中提示；
         *   · 抛给页面，让群成员面板在"拉人 / 踢人"后强刷一次 ——
         *     服务端不推送成员变化，这是唯一的通知口子。
         * 建群 / 拉人 / 踢人 / 解散各一条，正文格式见 api-group.md §9.5。
         */
        if (message.group && message.fromUid == 1)
            emit groupSystemMessage(message.groupId, message.body);

        if (peer == m_active_uid)
        {
            active_has_new = true;

            // 窗口在后台 / 不在「会话」分区：当没看见，照常攒未读，切回来再清
            if (attention())
            {
                active_got_message = true;
            }
            else
            {
                unread_delta[peer] += 1;
                qInfo() << "[chat] 人不在看这个会话（窗口后台或不在会话分区），"
                           "消息先记为未读:" << peer;
            }
        }
        else
        {
            unread_delta[peer] += 1;
        }

        ids.append(message.id);
    }

    if (skipped > 0)
        qInfo() << "[chat] 本批" << skipped << "条是 PEL 重投的旧消息，已去重";

    for (auto it = unread_delta.cbegin(); it != unread_delta.cend(); ++it)
    {
        const int index = indexOf(it.key());
        if (index < 0)
            continue;

        QVariantMap item = m_conversations.at(index).toMap();
        item.insert(QStringLiteral("unread"), item.value(QStringLiteral("unread")).toInt() + it.value());
        m_conversations[index] = item;

        // 未读落库（touchConversation 那次写的是加未读之前的值）
        persistConversation(it.key());
    }

    if (active_got_message)
    {
        // 人正看着这个会话：收到就算已读，不攒角标
        markRead(m_active_uid);
    }

    // 后台收到的那条也要刷进列表 —— 切回前台时人得能看见（红点由上面那步清）
    if (active_got_message || active_has_new)
        emit messagesChanged();

    emit conversationsChanged();

    /*
     * 确认（1025）：服务端 XACKDEL 之后这条才真正消失，否则下一轮 PEL 还会送回来。
     * 重复的也要确认（上面已经放进 ids）。服务端单次上限 10 个，保险起见分批。
     */
    for (int offset = 0; offset < ids.size();
         offset += mychat::protocol::MAX_CHAT_MESSAGES_PER_BATCH)
    {
        mychat::ChatSocket::instance().ackChatMessages(
            ids.mid(offset, mychat::protocol::MAX_CHAT_MESSAGES_PER_BATCH));
    }

    /*
     * 回来的条数达到这次要的条数 → 服务端那边（PEL 或新消息）可能还有：
     * 隔一小会儿再拉，别空转也不要干等 2s。
     * ⚠ 判据是"本次请求的 count"，不是硬编码 10 —— PEL 里可能只有 3 条，
     *   拉了 3 条回来也该继续把下一批新消息拉走。
     */
    if (m_pull_count > 0 && messages.size() >= m_pull_count)
    {
        QTimer::singleShot(kPullBacklogDelayMs, this, &ChatController::requestPull);
    }
    else if (m_pull_again || m_pull_hint > 0)
    {
        m_pull_again = false;
        requestPull();
    }
}

void ChatController::ensureConversation(const QString& peerUid)
{
    if (peerUid.isEmpty() || indexOf(peerUid) >= 0)
        return;

    // 只可能是"陌生人发来消息"（服务端未校验好友关系）：先用 uid 占位，
    // 等下次好友列表同步时再补昵称 / 备注
    QVariantMap item;
    item.insert(QStringLiteral("uid"), peerUid);
    item.insert(QStringLiteral("name"), peerUid);
    item.insert(QStringLiteral("nick"), QString());
    item.insert(QStringLiteral("displayName"), peerUid);
    item.insert(QStringLiteral("icon"), QString());
    item.insert(QStringLiteral("desc"), QString());
    item.insert(QStringLiteral("sex"), 2);
    item.insert(QStringLiteral("kind"), QStringLiteral("user"));
    item.insert(QStringLiteral("gid"), QString());
    item.insert(QStringLiteral("lastText"), QString());
    item.insert(QStringLiteral("lastTime"), QString());
    item.insert(QStringLiteral("unread"), 0);
    item.insert(QStringLiteral("demo"), false);
    m_conversations.prepend(item);
}

QString ChatController::senderNameFor(const QString& uid) const
{
    if (uid.isEmpty())
        return {};

    // 备注优先（displayName 已经是 "nick 非空 ? nick : name"），没有就退回 uid
    const QString name = m_friend_names.value(uid);
    return name.isEmpty() ? uid : name;
}

void ChatController::ensureGroupConversation(const QString& gid, const QString& name)
{
    if (gid.isEmpty())
        return;

    const QString key = groupKey(gid);
    const int index = indexOf(key);

    if (index >= 0)
    {
        /*
         * 会话已经在：只有"拿到了更好的名字"才更新。
         * 服务端不会推群资料，群名可能来自三个地方，优先级从高到低：
         *   · registerGroup 传入的（建群时用户自己填的）；
         *   · 本地 groups 表里存过的；
         *   · 「群 <gid>」占位。
         */
        if (name.isEmpty())
            return;

        QVariantMap item = m_conversations.at(index).toMap();
        if (item.value(QStringLiteral("displayName")).toString() == name)
            return;

        item.insert(QStringLiteral("name"), name);
        item.insert(QStringLiteral("displayName"), name);
        m_conversations[index] = item;
        if (m_active_uid == key)
            m_active_name = name;
        emit conversationsChanged();
        return;
    }

    /*
     * 新群会话：名字先看本地 groups 表（服务端没有查询接口，那是唯一来源），
     * 再没有就占位成「群 <gid>」—— 空名字会让会话行只剩一个头像，像坏了一样。
     */
    QString display = name;
    if (display.isEmpty())
    {
        for (const ChatStore::Group& group: m_store.groups())
        {
            if (group.gid == gid)
            {
                display = group.name;
                break;
            }
        }
    }
    if (display.isEmpty())
        display = QStringLiteral("群 %1").arg(gid);

    QVariantMap item;
    item.insert(QStringLiteral("uid"), key);
    item.insert(QStringLiteral("gid"), gid);
    item.insert(QStringLiteral("kind"), QStringLiteral("group"));
    item.insert(QStringLiteral("name"), display);
    item.insert(QStringLiteral("nick"), QString());
    item.insert(QStringLiteral("displayName"), display);
    item.insert(QStringLiteral("icon"), QString());
    item.insert(QStringLiteral("desc"), QString());
    item.insert(QStringLiteral("sex"), 2);
    item.insert(QStringLiteral("lastText"), QString());
    item.insert(QStringLiteral("lastTime"), QString());
    item.insert(QStringLiteral("unread"), 0);
    item.insert(QStringLiteral("demo"), false);
    m_conversations.prepend(item);
}

void ChatController::registerGroup(const QString& gid, const QString& name, const int role)
{
    if (gid.isEmpty())
    {
        qWarning() << "[chat] registerGroup 收到空群号，已忽略";
        return;
    }

    // 重新登记（例如被重新拉回同一个群）时解除"本地已移除"的闸
    m_removed_gids.remove(gid);

    ChatStore::Group group;
    group.gid = gid;
    group.name = name;
    group.role = role;
    group.updatedMs = QDateTime::currentMSecsSinceEpoch();
    if (m_store.isOpen())
        m_store.saveGroup(group);

    ensureGroupConversation(gid, name);

    const QString key = groupKey(gid);
    persistConversation(key);
    emit conversationsChanged();

    qInfo() << "[chat] 已登记群会话, gid =" << gid << "name =" << name << "role =" << role;
}

void ChatController::removeGroupConversation(const QString& gid)
{
    if (gid.isEmpty())
        return;

    const QString key = groupKey(gid);

    /*
     * 标记"本地已移除"：服务端的解散系统消息可能还在离线流里，
     * 稍后拉到时要忽略，不能把会话复活（见 onChatMessages 的闸）。
     * 即使当前列表里没有这个会话也要记 —— 系统消息一样会把它建出来。
     */
    m_removed_gids.insert(gid);

    const int index = indexOf(key);
    if (index < 0)
        return;

    /*
     * 活动会话正好是这个群 → 先关掉。注意顺序：
     * closeConversation() 会 emit activeConversationChanged，订阅方（ChatPage）
     * 可能立刻去打开列表里的第一个会话 —— 必须在从列表里删掉它**之前**关，
     * 否则订阅方会拿一个已经不在列表里的 uid 去 openConversation。
     */
    if (m_active_uid == key)
        closeConversation();

    m_conversations.removeAt(index);
    m_history.remove(key);
    m_history_loaded.remove(key);
    m_history_has_more.remove(key);

    // 群资料一并删掉；消息记录保留（服务端解散时也不清理任何人的记录）
    if (m_store.isOpen())
        m_store.removeGroup(gid);

    emit conversationsChanged();
    emit messagesChanged();

    qInfo() << "[chat] 已移除群会话, gid =" << gid;
}

void ChatController::appendIncoming(const QString& peerUid, const QString& text, const QString& time)
{
    if (peerUid.isEmpty() || text.isEmpty())
        return;

    const QString stamp = time.isEmpty() ? nowText() : time;

    // 会话不存在说明是个新联系人：先建一行，否则消息没地方落
    ensureConversation(peerUid);

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
            persistConversation(peerUid);
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

        // 状态也要落库：重启后失败的气泡仍然标红
        m_store.updateMessageState(messageId, state);

        if (peerUid == m_active_uid)
            emit messagesChanged();
        return;
    }
}

int ChatController::appendMessage(const QString& peerUid, const bool from_self,
                                  const QString& text, const QString& time,
                                  const QString& state, const qint64 created_ms,
                                  const QString& server_id, const QString& from_uid)
{
    const qint64 created = created_ms > 0 ? created_ms : QDateTime::currentMSecsSinceEpoch();

    ChatStore::Message row;
    row.peerUid = peerUid;
    row.fromSelf = from_self;
    row.fromUid = from_uid;
    row.body = text;
    row.type = 1;
    row.createdMs = created;
    row.serverId = server_id;
    row.state = state;

    int id = 0;
    switch (m_store.insertMessage(row))
    {
    case ChatStore::InsertResult::Duplicate:
        // 库里有同一个 server_id：PEL 重投（可能是上个进程留下的），不该再显示
        return 0;

    case ChatStore::InsertResult::Inserted:
        // 直接用库的自增 id 当界面消息 id —— 重启后读回来的 id 也是它，状态能对上
        id = static_cast<int>(row.id);
        break;

    case ChatStore::InsertResult::Failed:
        // 没开库 / 写失败：退化成内存 id，消息照常显示
        id = m_next_message_id++;
        break;
    }

    QVariantMap item = makeMessage(id, from_self, text, time, state, created, from_uid);
    /*
     * 群操作系统消息（from_uid "1"）：界面按居中灰字提示渲染，不是某个人的气泡。
     * 这里同时把 senderName 清空 —— senderNameFor("1") 会退回 "1"，
     * 让气泡一侧莫名出现"1 说："；系统消息本来就不该有发送者名字。
     */
    const bool system = !from_self && isGroupKey(peerUid) && from_uid == QLatin1String("1");
    item.insert(QStringLiteral("system"), system);
    // 群聊气泡要显示"这条是谁说的"；自己发的 / 系统消息不需要名字
    item.insert(QStringLiteral("senderName"),
                (from_self || system) ? QString() : senderNameFor(from_uid));
    m_history[peerUid].append(item);
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

    // 摘要跟着落库：重启后会话列表才有内容
    persistConversation(peerUid);
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

void ChatController::clearTip()
{
    setTip(QString());
}