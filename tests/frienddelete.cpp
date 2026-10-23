// 删除好友链路自检（1027 / 1028）—— 自带假 ChatServer，不需要真实服务端。
//
// 服务端 2026-10-19 的提交（`1914008`）新增了这条链路，配套的红线有四条：
//   1. 1027 请求体只有 `uid`，且必须是**字符串**（其余编号字段的约定一致）；
//   2. 1028 error=0 → 控制器立刻把这条从好友列表摘掉，不等下一次 1020；
//   3. 1028 error=1022（本来不是好友）→ 列表不动、提示"你们还不是好友"；
//   4. 本地已经不是好友时**不再发 1027**（省一次必然失败的往返）。
//
// 为什么不用真实服务端：真删一次就破坏测试账号的好友关系，而且 1022 场景
// 没法在"列表里还有这个人"的前提下稳定复现。
//
// 用法（无需服务端、无需网络）：
//   qtClientFriendDeleteTest
//
// 可选：对**真实服务端**做一次无损验证（只对非好友发 1027，期望 1022）：
//   qtClientFriendDeleteTest --live <uid> <pwd> [peer_uid]
//   目标若在好友列表里 → exit 6，绝不做破坏性删除。
//
// 退出码：0 全部通过；2 参数错误；3 有断言失败；4 超时（某一步没等到结果）；
//        5 起不了假服务器；6 live 模式的目标是真实好友，拒绝执行。

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QByteArray>
#include <QVariantList>

#include <cstdio>
#include <cstdint>
#include <functional>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "ChatSocket.h"
#include "ContactController.h"
#include "HttpClient.h"
#include "SessionManager.h"
#include "logger.h"

namespace
{
constexpr std::uint16_t kLoginReq = 1001;
constexpr std::uint16_t kLoginRsp = 1002;
constexpr std::uint16_t kSyncFriendListReq = 1020;
constexpr std::uint16_t kSyncFriendListRsp = 1021;
constexpr std::uint16_t kDelFriendReq = 1027;
constexpr std::uint16_t kDelFriendRsp = 1028;

constexpr int kOverallTimeoutMs = 20000;

const char* kSelfUid = "666672";
const char* kFriendA = "666673";
const char* kFriendB = "666674";

void writeLine(FILE* stream, const QString& text)
{
    const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
    std::fflush(stream);
}

/**
 * 假 ChatServer：只处理本测试用到的四帧，其余照收不答。
 *
 * 帧格式与真实服务端一致：[message_id:2 大端][body_len:2 大端][body]。
 */
class FakeServer
{
public:
    FakeServer()
    {
        QObject::connect(&m_server, &QTcpServer::newConnection, [this]()
                             {
                                 m_peer = m_server.nextPendingConnection();
                                 if (m_peer == nullptr)
                                     return;

                                 QObject::connect(m_peer, &QTcpSocket::readyRead, [this]()
                                                      {
                                                          m_buffer.append(m_peer->readAll());
                                                          dispatch();
                                                      });
                             });
    }

    [[nodiscard]] bool listen() { return m_server.listen(QHostAddress::LocalHost, 0); }
    [[nodiscard]] quint16 port() const { return m_server.serverPort(); }

    std::function<void(std::uint16_t, const QByteArray&)> onFrame;

    QList<QPair<std::uint16_t, QByteArray>> frames;

    void reply(const std::uint16_t id, const QByteArray& body)
    {
        if (m_peer == nullptr)
            return;

        QByteArray frame;
        frame.reserve(4 + body.size());
        frame.append(static_cast<char>((id >> 8) & 0xFF));
        frame.append(static_cast<char>(id & 0xFF));
        const auto len = static_cast<std::uint16_t>(body.size());
        frame.append(static_cast<char>((len >> 8) & 0xFF));
        frame.append(static_cast<char>(len & 0xFF));
        frame.append(body);

        m_peer->write(frame);
        m_peer->flush();
    }

    [[nodiscard]] int countOf(const std::uint16_t id) const
    {
        int count = 0;
        for (const auto& frame: frames)
        {
            if (frame.first == id)
                ++count;
        }
        return count;
    }

private:
    void dispatch()
    {
        for (;;)
        {
            if (m_buffer.size() < 4)
                return;

            const auto* head = reinterpret_cast<const unsigned char*>(m_buffer.constData());
            const auto id = static_cast<std::uint16_t>((head[0] << 8) | head[1]);
            const auto len = static_cast<std::uint16_t>((head[2] << 8) | head[3]);

            if (m_buffer.size() < 4 + len)
                return;

            const QByteArray body = m_buffer.mid(4, len);
            m_buffer.remove(0, 4 + len);

            frames.append({id, body});

            if (onFrame)
                onFrame(id, body);
        }
    }

    QTcpServer m_server;
    QTcpSocket* m_peer = nullptr;
    QByteArray m_buffer;
};

/**
 * 校验 1027 的请求体：只允许一个 `uid` 字段、且必须是字符串。
 *
 * @return 空串 = 合法；否则是一条人话错误描述
 */
QString checkDelFriendBody(const QByteArray& body, const QString& expect_uid)
{
    QJsonParseError parse_error{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
        return QStringLiteral("1027 请求体不是合法 JSON 对象: %1").arg(QString::fromUtf8(body));

    const QJsonObject object = document.object();

    /*
     * 2026-10-22 起所有请求都带客户端请求编号 `msgid`（ChatSocket::sendJsonWithId 统一注入），
     * 所以合法字段是 `uid` + `msgid` 两个 —— 这里一并守住，免得哪天 msgid 被漏掉。
     */
    const QStringList keys = object.keys();
    if (keys.size() != 2 || !keys.contains(QStringLiteral("uid"))
        || !keys.contains(QStringLiteral("msgid")))
        return QStringLiteral("1027 请求体应当是 uid + msgid: %1")
            .arg(QString::fromUtf8(body));

    if (!object.value(QStringLiteral("msgid")).toString().startsWith(QLatin1String("r-")))
        return QStringLiteral("1027 的 msgid 必须是以 r- 开头的字符串: %1")
            .arg(QString::fromUtf8(body));

    if (!object.value(QStringLiteral("uid")).isString())
        return QStringLiteral("1027 的 uid 必须是字符串: %1").arg(QString::fromUtf8(body));

    if (object.value(QStringLiteral("uid")).toString() != expect_uid)
        return QStringLiteral("1027 的 uid 不对: 期望 %1，实际 %2")
            .arg(expect_uid, object.value(QStringLiteral("uid")).toString());

    return {};
}

/**
 * 对**真实服务端**跑一次无损验证：
 *
 *   qtClientFriendDeleteTest --live <uid> <pwd> [peer_uid]
 *
 * 先拉好友列表确认 peer **不是**好友，才发 1027；期望 1028 带 error=1022。
 * 目标若在好友列表里 → 直接退出（exit 6），这个模式绝不做破坏性删除。
 */
int run_live(QCoreApplication& app, const QString& uid_text, const QString& pwd,
             const QString& peer_text)
{
    auto& socket = mychat::ChatSocket::instance();

    int fail_count = 0;
    const auto fail = [&](const QString& why)
        {
            writeLine(stderr, QStringLiteral("[FAIL] %1").arg(why));
            ++fail_count;
        };
    const auto check = [&](const bool ok, const QString& what)
        {
            if (ok)
            {
                writeLine(stdout, QStringLiteral("[OK] %1").arg(what));
                return true;
            }
            fail(what);
            return false;
        };

    QString token;
    std::uint64_t self_uid = 0;

    QTimer overall;
    overall.setSingleShot(true);
    QObject::connect(&overall, &QTimer::timeout, &app, [&]()
                         {
                             fail(QStringLiteral("整体超时"));
                             app.exit(3);
                         });
    overall.start(kOverallTimeoutMs);

    QObject::connect(&socket, &mychat::ChatSocket::connected, &app, [&]()
                         {
                             writeLine(stdout, QStringLiteral("[OK] 已连接 ChatServer"));
                             socket.login(self_uid, token);
                         });

    QObject::connect(&socket, &mychat::ChatSocket::loginFailed, &app, [&](const int error)
                         {
                             fail(QStringLiteral("ChatServer 登录失败, error=%1").arg(error));
                             app.exit(3);
                         });

    QObject::connect(&socket, &mychat::ChatSocket::loginSucceeded, &app,
                     [&](const std::uint64_t uid, const QString& name, const QString& tok)
                         {
                             mychat::SessionManager::instance().update(uid, name, tok);
                             socket.syncFriendList();
                         });

    QObject::connect(&socket, &mychat::ChatSocket::friendListResult, &app,
                     [&](const QList<mychat::protocol::FriendInfo>& friends, const int error)
                         {
                             if (error != 0)
                             {
                                 fail(QStringLiteral("拉好友列表失败, error=%1").arg(error));
                                 app.exit(3);
                                 return;
                             }

                             for (const auto& f: friends)
                             {
                                 if (QString::number(f.uid) == peer_text)
                                 {
                                     writeLine(stderr,
                                               QStringLiteral(
                                                   "[ABORT] %1 是真实好友，live 模式不做破坏性删除")
                                               .arg(peer_text));
                                     app.exit(6);
                                     return;
                                 }
                             }

                             writeLine(stdout, QStringLiteral("[OK] %1 不在好友列表里，安全")
                                      .arg(peer_text));
                             writeLine(stdout, QStringLiteral("[..] 发送 1027 ..."));
                             socket.delFriend(peer_text);
                         });

    QObject::connect(&socket, &mychat::ChatSocket::friendDeleteResult, &app,
                     [&](const bool ok, const int error, const QString& uid)
                         {
                             check(!ok, QStringLiteral("不是好友时 ok=false"));
                             check(error == 1022, QStringLiteral("真实服务端回 1022 NOT_FRIEND")
                                   + QStringLiteral("（实际 %1）").arg(error));
                             check(uid == peer_text, QStringLiteral("1028 回显 uid"));
                             app.exit(fail_count == 0 ? 0 : 3);
                         });

    QJsonObject body;
    body.insert(QStringLiteral("user"), uid_text);
    body.insert(QStringLiteral("password"), pwd);

    mychat::HttpClient::instance().setBaseUrl(QStringLiteral("http://127.0.0.1:8080"));
    mychat::HttpClient::instance().post(QStringLiteral("/user_login"), body,
                                       [&](const bool ok, const QJsonObject& rsp)
                                       {
                                           if (!ok || rsp.value(QStringLiteral("error")).toInt(-1) != 0)
                                           {
                                               fail(QStringLiteral("HTTP 登录失败: %1")
                                                    .arg(QString::fromUtf8(
                                                        QJsonDocument(rsp).toJson(
                                                            QJsonDocument::Compact))));
                                               app.exit(3);
                                               return;
                                           }

                                           const auto uid = mychat::protocol::parseUid(
                                               rsp.value(QStringLiteral("uid")));
                                           token = rsp.value(QStringLiteral("token")).toString();
                                           const QString host = rsp.value(QStringLiteral("host"))
                                                                    .toString();
                                           bool port_ok = false;
                                           const std::uint16_t port =
                                               rsp.value(QStringLiteral("port")).toString()
                                                  .toUShort(&port_ok);

                                           if (!uid.has_value() || token.isEmpty() || host.isEmpty()
                                               || !port_ok || port == 0)
                                           {
                                               fail(QStringLiteral("登录回包字段不全: %1")
                                                    .arg(QString::fromUtf8(
                                                        QJsonDocument(rsp).toJson(
                                                            QJsonDocument::Compact))));
                                               app.exit(3);
                                               return;
                                           }

                                           self_uid = *uid;
                                           writeLine(stdout,
                                                     QStringLiteral("[OK] HTTP 登录成功，聊天服 %1:%2")
                                                     .arg(host).arg(port));
                                           socket.connectToServer(host, port);
                                       });

    return app.exec();
}
} // namespace

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif

    QCoreApplication app(argc, argv);
    mychat::log::init();

    /*
     * 可选：对真实服务端做无损验证（只删非好友，目标若是好友直接拒绝）。
     *   qtClientFriendDeleteTest --live <uid> <pwd> [peer_uid]
     */
    if (argc >= 4 && QString::fromUtf8(argv[1]) == QStringLiteral("--live"))
    {
        const QString peer = argc >= 5 ? QString::fromUtf8(argv[4])
                                       : QStringLiteral("999999999");
        return run_live(app, QString::fromUtf8(argv[2]), QString::fromUtf8(argv[3]), peer);
    }

    FakeServer server;
    if (!server.listen())
    {
        writeLine(stderr, QStringLiteral("[FAIL] 起不了本地假服务器"));
        return 5;
    }
    writeLine(stdout, QStringLiteral("[setup] 假 ChatServer 已就绪，端口 %1").arg(server.port()));

    auto& socket = mychat::ChatSocket::instance();
    ContactController contact;

    int fail_count = 0;
    const auto fail = [&](const QString& why)
        {
            writeLine(stderr, QStringLiteral("[FAIL] %1").arg(why));
            ++fail_count;
        };
    const auto check = [&](const bool ok, const QString& what)
        {
            if (ok)
            {
                writeLine(stdout, QStringLiteral("[OK] %1").arg(what));
                return true;
            }
            fail(what);
            return false;
        };

    QTimer overall;
    overall.setSingleShot(true);
    QObject::connect(&overall, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 整体超时（某一步没等到结果）"));
                             app.exit(4);
                         });

    // 假服务器的行为脚本
    int del_frames = 0;

    server.onFrame = [&](const std::uint16_t id, const QByteArray& body)
        {
            if (id == kLoginReq)
            {
                const QString json = QStringLiteral(
                    R"({"error":0,"uid":"%1","token":"fake-token","name":"tester"})")
                    .arg(QString::fromLatin1(kSelfUid));
                server.reply(kLoginRsp, json.toUtf8());
                return;
            }

            if (id == kSyncFriendListReq)
            {
                // 两个好友，字段形状与服务端 1021 一致（sex 是 number、uid 是 string）
                const QString json = QStringLiteral(
                    R"({"error":0,"count":"2","friends":[)"
                    R"({"uid":"%1","name":"好友A","desc":"","nick":"A","sex":0,"icon":""},)"
                    R"({"uid":"%2","name":"好友B","desc":"","nick":"","sex":1,"icon":""}]})")
                    .arg(QString::fromLatin1(kFriendA), QString::fromLatin1(kFriendB));
                server.reply(kSyncFriendListRsp, json.toUtf8());
                return;
            }

            if (id == kDelFriendReq)
            {
                ++del_frames;

                if (del_frames == 1)
                {
                    const QString err = checkDelFriendBody(body, QString::fromLatin1(kFriendA));
                    if (err.isEmpty())
                        writeLine(stdout, QStringLiteral("[OK] 1027 请求体是 uid + msgid，且都是字符串"));
                    else
                        fail(err);

                    /*
                     * 第一次回包**回显 msgid**（真实服务端 2026-10-22 起的行为），
                     * 验证按编号精确认领这条主路径。
                     */
                    const QString req_msgid = QJsonDocument::fromJson(body)
                                                  .object()
                                                  .value(QStringLiteral("msgid"))
                                                  .toString();
                    server.reply(kDelFriendRsp,
                                 QStringLiteral(R"({"error":0,"msgid":"%1","uid":"%2"})")
                                 .arg(req_msgid, QString::fromLatin1(kFriendA)).toUtf8());
                    return;
                }

                /*
                 * 第二次：模拟"服务端认为关系不存在" **且回包不带 msgid**
                 * （老服务端 / 回显缺失）—— 验证仍能按同型号 FIFO 兜底认领。
                 */
                server.reply(kDelFriendRsp,
                             QStringLiteral(R"({"error":1022,"uid":"%1"})")
                             .arg(QString::fromLatin1(kFriendB)).toUtf8());
            }
        };

    // 登录成功 → 把会话写进 SessionManager（真实客户端由 LoginController 做这件事）
    QObject::connect(&socket, &mychat::ChatSocket::loginSucceeded, &app,
                     [&](const std::uint64_t uid, const QString& name, const QString& token)
                         {
                             mychat::SessionManager::instance().update(uid, name, token);
                             contact.refreshFriendList();
                         });

    // 好友列表到位 → 开始删第一个
    QObject::connect(&contact, &ContactController::friendListRefreshed, &app,
                     [&](const bool ok, const int error)
                         {
                             if (!check(ok, QStringLiteral("好友列表刷新成功（error=%1）").arg(error)))
                             {
                                 app.exit(3);
                                 return;
                             }

                             if (!check(contact.friendCount() == 2,
                                        QStringLiteral("好友列表 2 位")))
                             {
                                 app.exit(3);
                                 return;
                             }

                             /*
                              * 延到下一拍再删：此刻 busy 还没放开（onFriendList 里
                              * setBusy(false) 在 emit 之后），真实用户点按钮时早就不是这个时机了。
                              */
                             QTimer::singleShot(0, &app, [&]()
                                                    {
                                                        contact.removeFriend(
                                                            QString::fromLatin1(kFriendA));
                                                    });
                         });

    // 删除结果：第一次成功，第二次 1022
    int delete_results = 0;
    QObject::connect(&contact, &ContactController::friendDeleteFinished, &app,
                     [&](const bool ok, const int error, const QString& uid)
                         {
                             ++delete_results;

                             if (delete_results == 1)
                             {
                                 if (!check(ok && error == 0,
                                            QStringLiteral("第一次删除成功（error=%1）").arg(error)))
                                 {
                                     app.exit(3);
                                     return;
                                 }

                                 check(contact.friendCount() == 1,
                                       QStringLiteral("好友列表立刻少了一位（不等下一次 1020）"));
                                 check(!contact.isFriend(QString::fromLatin1(kFriendA)),
                                       QStringLiteral("isFriend 对被删的人返回 false"));

                                 /*
                                  * 本地已经不是好友 → 再删一次不该发 1027。
                                  *
                                  * ⚠ 必须延到下一拍：此刻 busy 还没放开（setBusy(false) 在
                                  *   emit friendDeleteFinished 之后），直接调会被 busy 分支挡掉，
                                  *   断言就会"通过得莫名其妙"。
                                  */
                                 QTimer::singleShot(0, &app, [&]()
                                                        {
                                                            const int before =
                                                                server.countOf(kDelFriendReq);
                                                            contact.removeFriend(
                                                                QString::fromLatin1(kFriendA));
                                                            check(server.countOf(kDelFriendReq) == before,
                                                                  QStringLiteral(
                                                                      "本地已不是好友时不再发 1027"));
                                                            check(contact.tip().contains(
                                                                      QStringLiteral("还不是好友")),
                                                                  QStringLiteral(
                                                                      "本地拒绝时给出「还不是好友」提示"));

                                                            // 第二步：删第二个，让服务端回 1022
                                                            contact.removeFriend(
                                                                QString::fromLatin1(kFriendB));
                                                        });
                                 return;
                             }

                             if (delete_results == 2)
                             {
                                 check(!ok && error == 1022,
                                       QStringLiteral("服务端回 1022 时按失败上报（error=%1）")
                                       .arg(error));
                                 check(contact.friendCount() == 1,
                                       QStringLiteral("1022 时好友列表保持不变"));
                                 check(contact.isFriend(QString::fromLatin1(kFriendB)),
                                       QStringLiteral("1022 时这条好友仍在列表里"));

                                 if (fail_count == 0)
                                     writeLine(stdout,
                                               QStringLiteral(
                                                   "\n[PASS] 删除好友链路（请求格式 / 列表同步 / 1022）全部通过"));
                                 app.exit(fail_count == 0 ? 0 : 3);
                             }
                         });

    QObject::connect(&socket, &mychat::ChatSocket::loginFailed, &app, [&](const int error)
                         {
                             fail(QStringLiteral("登录失败，error=%1").arg(error));
                             app.exit(3);
                         });

    QObject::connect(&socket, &mychat::ChatSocket::connected, &app, [&]()
                         {
                             writeLine(stdout, QStringLiteral("[OK] 已连接假服务器"));
                             socket.login(666672, QStringLiteral("fake-token"));
                         });

    overall.start(kOverallTimeoutMs);
    QTimer::singleShot(0, &app, [&]()
                           {
                               socket.connectToServer(QStringLiteral("127.0.0.1"),
                                                      static_cast<std::uint16_t>(server.port()));
                           });

    const int result = app.exec();
    writeLine(stdout, QStringLiteral("[exit] %1").arg(result));
    return result;
}
