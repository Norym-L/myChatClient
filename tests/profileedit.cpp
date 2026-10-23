// 好友备注 / 编辑资料链路自检（1029 / 1030、1031 / 1032）—— 自带假 ChatServer，不需要真实服务端。
//
// 服务端 2026-10-21（基线 `5524366`）新增了这两条链路，配套的红线：
//   1. 1029 请求体是 `{id, friend_uid, remark}`，uid 必须是**字符串**、remark 允许空串（清空）；
//   2. 1030 error=0 → 控制器**立刻**把新备注写进 friendList（nick + displayName），
//      不等下一次 1020；会话列表由 friendListChanged → syncFriends 跟着改名；
//   3. 1030 error=1022（不是好友 / 给自己设置）→ 列表不动、提示是错误色；
//   4. 1031 请求体是 `{id, name, desc, sex}`，sex 是**数字**，且**不带 icon**
//      （服务端没有头像上传通道，发空 icon 会把已有地址覆盖掉）；
//   5. 1032 error=0 → LoginController 的昵称 / 签名 / 性别立刻更新；
//   6. 1032 error=1011 → 昵称保持原值（不能"请求失败了界面却当成功"）。
//
// 为什么不用真实服务端：改备注会污染测试账号的好友数据，1032 改昵称还会影响
// 其它自检的断言；而 1022 / 1011 这两条失败分支在真实环境里没法稳定复现。
//
// 用法（无需服务端、无需网络）：
//   qtClientProfileEditTest
//
// 退出码：0 全部通过；3 有断言失败；4 超时（某一步没等到结果）；5 起不了假服务器。

#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLatin1Char>
#include <QString>
#include <QStringList>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <cstdint>
#include <cstdio>
#include <functional>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "ChatSocket.h"
#include "ContactController.h"
#include "LoginController.h"
#include "SessionManager.h"
#include "logger.h"

namespace
{
constexpr std::uint16_t kLoginReq = 1001;
constexpr std::uint16_t kLoginRsp = 1002;
constexpr std::uint16_t kSyncFriendListReq = 1020;
constexpr std::uint16_t kSyncFriendListRsp = 1021;
constexpr std::uint16_t kSetRemarkReq = 1029;
constexpr std::uint16_t kSetRemarkRsp = 1030;
constexpr std::uint16_t kChangeInfoReq = 1031;
constexpr std::uint16_t kChangeInfoRsp = 1032;

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

QJsonObject parseObject(const QByteArray& body)
{
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return {};
    return document.object();
}

/**
 * 假 ChatServer：只处理本测试用到的五帧，其余照收不答。
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

/// 校验 1029 请求体：msgid / friend_uid / remark，uid 与 remark 都必须是字符串
QString checkRemarkBody(const QByteArray& body, const QString& expect_uid,
                        const QString& expect_remark)
{
    const QJsonObject object = parseObject(body);
    if (object.isEmpty() && !body.contains('{'))
        return QStringLiteral("1029 请求体不是合法 JSON 对象: %1").arg(QString::fromUtf8(body));

    const QStringList keys = object.keys();
    if (keys.size() != 3 || !keys.contains(QStringLiteral("msgid"))
        || !keys.contains(QStringLiteral("friend_uid"))
        || !keys.contains(QStringLiteral("remark")))
        return QStringLiteral("1029 请求体应当只有 msgid / friend_uid / remark: %1")
            .arg(QString::fromUtf8(body));

    // 2026-10-22 起统一用 msgid（服务端不再认单独的 id）；它必须非空，否则回包认不回来
    const QString msgid = object.value(QStringLiteral("msgid")).toString();
    if (!object.value(QStringLiteral("msgid")).isString() || !msgid.startsWith(QLatin1String("r-")))
        return QStringLiteral("1029 的 msgid 必须是非空字符串且以 r- 开头: %1")
            .arg(QString::fromUtf8(body));

    if (!object.value(QStringLiteral("friend_uid")).isString())
        return QStringLiteral("1029 的 friend_uid 必须是字符串: %1").arg(QString::fromUtf8(body));

    if (!object.value(QStringLiteral("remark")).isString())
        return QStringLiteral("1029 的 remark 必须是字符串（空串表示清空）: %1")
            .arg(QString::fromUtf8(body));

    if (object.value(QStringLiteral("friend_uid")).toString() != expect_uid)
        return QStringLiteral("1029 的 friend_uid 不对: 期望 %1，实际 %2")
            .arg(expect_uid, object.value(QStringLiteral("friend_uid")).toString());

    if (object.value(QStringLiteral("remark")).toString() != expect_remark)
        return QStringLiteral("1029 的 remark 不对: 期望「%1」，实际「%2」")
            .arg(expect_remark, object.value(QStringLiteral("remark")).toString());

    return {};
}

/// 校验 1031 请求体：name / desc / sex，sex 必须是数字，且不能带 icon
QString checkChangeInfoBody(const QByteArray& body, const QString& expect_name, const int expect_sex)
{
    const QJsonObject object = parseObject(body);
    if (object.isEmpty() && !body.contains('{'))
        return QStringLiteral("1031 请求体不是合法 JSON 对象: %1").arg(QString::fromUtf8(body));

    const QStringList keys = object.keys();
    if (keys.contains(QStringLiteral("icon")))
        return QStringLiteral("1031 不应带 icon（服务端没有头像通道，空值会覆盖原地址）: %1")
            .arg(QString::fromUtf8(body));

    if (!keys.contains(QStringLiteral("msgid")) || !keys.contains(QStringLiteral("name"))
        || !keys.contains(QStringLiteral("desc")) || !keys.contains(QStringLiteral("sex")))
        return QStringLiteral("1031 缺字段（需要 msgid/name/desc/sex）: %1")
            .arg(QString::fromUtf8(body));

    if (!object.value(QStringLiteral("name")).isString())
        return QStringLiteral("1031 的 name 必须是字符串: %1").arg(QString::fromUtf8(body));

    if (!object.value(QStringLiteral("desc")).isString())
        return QStringLiteral("1031 的 desc 必须是字符串: %1").arg(QString::fromUtf8(body));

    if (!object.value(QStringLiteral("sex")).isDouble())
        return QStringLiteral("1031 的 sex 必须是数字（0 男 / 1 女 / 2 未知）: %1")
            .arg(QString::fromUtf8(body));

    if (object.value(QStringLiteral("name")).toString() != expect_name)
        return QStringLiteral("1031 的 name 不对: 期望 %1，实际 %2")
            .arg(expect_name, object.value(QStringLiteral("name")).toString());

    if (object.value(QStringLiteral("sex")).toInt() != expect_sex)
        return QStringLiteral("1031 的 sex 不对: 期望 %1，实际 %2")
            .arg(expect_sex).arg(object.value(QStringLiteral("sex")).toInt());

    return {};
}

} // namespace

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif

    QCoreApplication app(argc, argv);

    /*
     * 隔离凭据文件：改名会真的走 SessionManager::save()，不隔离就会改写用户自己的
     * session.ini。目录放在 exe 旁边 —— %TEMP% 在受限环境里不可写，症状会像功能坏了。
     * 必须在 SessionManager 第一次读文件之前设好（它在本测试里是登录后才用到的）。
     */
    const QString data_dir = QCoreApplication::applicationDirPath()
                             + QStringLiteral("/test-data");
    QDir().mkpath(data_dir);
    qputenv("MYCHAT_SESSION_FILE",
            (data_dir + QStringLiteral("/profileedit-session.ini")).toUtf8());

    mychat::log::init();    FakeServer server;
    if (!server.listen())
    {
        writeLine(stderr, QStringLiteral("[FAIL] 起不了本地假服务器"));
        return 5;
    }
    writeLine(stdout, QStringLiteral("[setup] 假 ChatServer 已就绪，端口 %1").arg(server.port()));

    auto& socket = mychat::ChatSocket::instance();
    ContactController contact;
    LoginController login;

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
    overall.start(kOverallTimeoutMs);

    const QString remark_a = QStringLiteral("老同学");

    /// 假服务器的行为脚本：按调用次数切换成功 / 失败
    int remark_frames = 0;
    int info_frames = 0;

    /*
     * 回包必须回显请求里的 msgid —— 客户端从 2026-10-22 起按它精确认领，
     * 不回显就会退回 FIFO 兜底，这条测试也就测不到真实链路了。
     */
    const auto reply_msgid = [](const QByteArray& raw_body)
    {
        return parseObject(raw_body).value(QStringLiteral("msgid")).toString();
    };

    server.onFrame = [&](const std::uint16_t id, const QByteArray& body)
        {
            if (id == kLoginReq)
            {
                /*
                 * LoginController 在 socket connected 时会用它自己那对
                 * （尚未赋值的）uid / token 主动发一次登录 —— 那是给「自动登录」
                 * 用的。这里直接忽略这份空凭据，只回应测试显式发的那一发。
                 */
                const QJsonObject request = parseObject(body);
                if (request.value(QStringLiteral("uid")).toString()
                    != QString::fromLatin1(kSelfUid))
                    return;

                const QString json = QStringLiteral(
                    R"({"error":0,"uid":"%1","token":"fake-token","name":"tester"})")
                    .arg(QString::fromLatin1(kSelfUid));
                server.reply(kLoginRsp, json.toUtf8());
                return;
            }

            if (id == kSyncFriendListReq)
            {
                // 两个好友：A 已有备注 "A"，B 没有备注（用来验证 displayName 回落昵称）
                const QString json = QStringLiteral(
                    R"({"error":0,"count":"2","friends":[)"
                    R"({"uid":"%1","name":"好友A","desc":"","nick":"A","sex":0,"icon":""},)"
                    R"({"uid":"%2","name":"好友B","desc":"","nick":"","sex":1,"icon":""}]})")
                    .arg(QString::fromLatin1(kFriendA), QString::fromLatin1(kFriendB));
                server.reply(kSyncFriendListRsp, json.toUtf8());
                return;
            }

            if (id == kSetRemarkReq)
            {
                ++remark_frames;

                if (remark_frames == 1)
                {
                    const QString err = checkRemarkBody(body, QString::fromLatin1(kFriendA),
                                                        remark_a);
                    if (err.isEmpty())
                        writeLine(stdout, QStringLiteral("[OK] 1029 请求体：friend_uid 字符串 + remark"));
                    else
                        fail(err);

                    server.reply(kSetRemarkRsp,
                                 QStringLiteral(R"({"error":0,"msgid":"%1"})")
                                 .arg(reply_msgid(body)).toUtf8());
                    return;
                }

                if (remark_frames == 2)
                {
                    // 服务端认为"不是好友"（Redis 缓存与 MySQL 不一致时也会这样）
                    server.reply(kSetRemarkRsp,
                                 QStringLiteral(R"({"error":1022,"msgid":"%1"})")
                                 .arg(reply_msgid(body)).toUtf8());
                    return;
                }

                // 第三次：清空备注（空串），应当被原样发出去
                const QString err = checkRemarkBody(body, QString::fromLatin1(kFriendA), QString());
                if (err.isEmpty())
                    writeLine(stdout, QStringLiteral("[OK] 1029 空备注按「清空」原样发送"));
                else
                    fail(err);

                server.reply(kSetRemarkRsp,
                             QStringLiteral(R"({"error":0,"msgid":"%1"})")
                             .arg(reply_msgid(body)).toUtf8());
                return;
            }

            if (id == kChangeInfoReq)
            {
                ++info_frames;

                if (info_frames == 1)
                {
                    const QString err = checkChangeInfoBody(body, QStringLiteral("新昵称"), 1);
                    if (err.isEmpty())
                        writeLine(stdout, QStringLiteral("[OK] 1031 请求体：name/desc/sex，无 icon"));
                    else
                        fail(err);

                    server.reply(kChangeInfoRsp,
                                 QStringLiteral(R"({"error":0,"msgid":"%1"})")
                                 .arg(reply_msgid(body)).toUtf8());
                    return;
                }

                // 第二次：存储过程回 -1（uid 不存在）→ 1011
                server.reply(kChangeInfoRsp,
                             QStringLiteral(R"({"error":1011,"msgid":"%1"})")
                             .arg(reply_msgid(body)).toUtf8());
            }
        };

    // 登录成功 → 写会话（真实客户端由 LoginController 自己做这件事）→ 拉好友列表
    QObject::connect(&socket, &mychat::ChatSocket::loginSucceeded, &app,
                     [&](const std::uint64_t uid, const QString& name, const QString& token)
                         {
                             mychat::SessionManager::instance().update(uid, name, token);
                             contact.refreshFriendList();
                         });

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

                             if (!check(contact.tip().contains(QStringLiteral("好友")),
                                        QStringLiteral("刷新后有提示")))
                             {
                                 app.exit(3);
                                 return;
                             }

                             // busy 还没放开（setBusy(false) 在 emit 之后），延一拍再改备注
                             QTimer::singleShot(0, &app, [&]()
                                                    {
                                                        contact.setFriendRemark(
                                                            QString::fromLatin1(kFriendA),
                                                            remark_a);
                                                    });
                         });

    int remark_results = 0;
    QObject::connect(&contact, &ContactController::friendRemarkSetFinished, &app,
                     [&](const bool ok, const int error, const QString& uid)
                         {
                             ++remark_results;

                             if (remark_results == 1)
                             {
                                 if (!check(ok && error == 0,
                                            QStringLiteral("设置备注成功（error=%1）").arg(error)))
                                 {
                                     app.exit(3);
                                     return;
                                 }

                                 /*
                                  * 服务端文档说"要重新拉 1020 才能看到新备注"，但客户端手上
                                  * 就有 uid + 新备注，直接就地更新 —— 会话列表立刻改名。
                                  */
                                 const QVariantList list = contact.friendList();
                                 QString nick;
                                 QString display;
                                 for (const QVariant& entry: list)
                                 {
                                     const QVariantMap item = entry.toMap();
                                     if (item.value(QStringLiteral("uid")).toString()
                                         == QString::fromLatin1(kFriendA))
                                     {
                                         nick = item.value(QStringLiteral("nick")).toString();
                                         display = item.value(QStringLiteral("displayName"))
                                                       .toString();
                                     }
                                 }

                                 check(nick == remark_a,
                                       QStringLiteral("friendList 里的 nick 立刻变成新备注"));
                                 check(display == remark_a,
                                       QStringLiteral("displayName 跟着备注走（不等下一次 1020）"));
                                 check(server.countOf(kSyncFriendListReq) == 1,
                                       QStringLiteral("没有偷偷再拉一次好友列表"));

                                 // 第二次：给 B 改备注，服务端回 1022
                                 QTimer::singleShot(0, &app, [&]()
                                                        {
                                                            contact.setFriendRemark(
                                                                QString::fromLatin1(kFriendB),
                                                                QStringLiteral("不该成功"));
                                                        });
                                 return;
                             }

                             if (remark_results == 2)
                             {
                                 check(!ok, QStringLiteral("1022 时 ok=false"));
                                 check(error == 1022, QStringLiteral("1022 原样上报"));
                                 check(contact.tipIsError(),
                                       QStringLiteral("失败提示是错误色"));

                                 const QVariantList list = contact.friendList();
                                 QString nick_b;
                                 for (const QVariant& entry: list)
                                 {
                                     const QVariantMap item = entry.toMap();
                                     if (item.value(QStringLiteral("uid")).toString()
                                         == QString::fromLatin1(kFriendB))
                                         nick_b = item.value(QStringLiteral("nick")).toString();
                                 }
                                 check(nick_b.isEmpty(),
                                       QStringLiteral("失败时好友列表一点没动"));

                                 // 第三次：清空 A 的备注
                                 QTimer::singleShot(0, &app, [&]()
                                                        {
                                                            contact.setFriendRemark(
                                                                QString::fromLatin1(kFriendA),
                                                                QString());
                                                        });
                                 return;
                             }

                             if (remark_results == 3)
                             {
                                 check(ok && error == 0,
                                       QStringLiteral("清空备注成功（error=%1）").arg(error));

                                 const QVariantList list = contact.friendList();
                                 QString display;
                                 QString nick;
                                 for (const QVariant& entry: list)
                                 {
                                     const QVariantMap item = entry.toMap();
                                     if (item.value(QStringLiteral("uid")).toString()
                                         == QString::fromLatin1(kFriendA))
                                     {
                                         nick = item.value(QStringLiteral("nick")).toString();
                                         display = item.value(QStringLiteral("displayName"))
                                                       .toString();
                                     }
                                 }
                                 check(nick.isEmpty(), QStringLiteral("nick 已清空"));
                                 check(display == QStringLiteral("好友A"),
                                       QStringLiteral("displayName 回落到昵称「好友A」"));

                                 // 接下来验证编辑资料（1031 / 1032）
                                 QTimer::singleShot(0, &app, [&]()
                                                        {
                                                            login.changeUserInfo(
                                                                QStringLiteral("新昵称"),
                                                                QStringLiteral("新签名"), 1);
                                                        });
                             }
                         });

    int info_results = 0;
    QObject::connect(&login, &LoginController::userInfoChangeFinished, &app,
                     [&](const bool ok, const int error)
                         {
                             ++info_results;

                             if (info_results == 1)
                             {
                                 if (!check(ok && error == 0,
                                            QStringLiteral("保存资料成功（error=%1）").arg(error)))
                                 {
                                     app.exit(3);
                                     return;
                                 }

                                 check(login.name() == QStringLiteral("新昵称"),
                                       QStringLiteral("LoginController.name 立刻改名"));
                                 check(login.selfDesc() == QStringLiteral("新签名"),
                                       QStringLiteral("selfDesc 缓存新签名"));
                                 check(login.selfSex() == 1,
                                       QStringLiteral("selfSex 缓存新性别（女）"));
                                 check(mychat::SessionManager::instance().name()
                                           == QStringLiteral("新昵称"),
                                       QStringLiteral("SessionManager 里的昵称同步（账号卡片显示它）"));

                                 // 第二次：服务端回 1011（uid 不存在 / 未登录）
                                 QTimer::singleShot(0, &app, [&]()
                                                        {
                                                            login.changeUserInfo(
                                                                QStringLiteral("另一个昵称"),
                                                                QString(), 2);
                                                        });
                                 return;
                             }

                             if (info_results == 2)
                             {
                                 check(!ok, QStringLiteral("1011 时 ok=false"));
                                 check(error == 1011, QStringLiteral("1011 原样上报"));
                                 check(login.name() == QStringLiteral("新昵称"),
                                       QStringLiteral("失败时昵称保持上一次成功的值"));

                                 writeLine(stdout, fail_count == 0
                                                ? QStringLiteral("[DONE] 全部通过")
                                                : QStringLiteral("[DONE] 有 %1 项失败")
                                                .arg(fail_count));
                                 app.exit(fail_count == 0 ? 0 : 3);
                             }
                         });

    // 登录：假服务器把 HTTP 那一步也省掉，直接连 TCP 并发 1001
    QObject::connect(&socket, &mychat::ChatSocket::connected, &app, [&]()
                         {
                             // 记下聊天服地址，改名时 SessionManager::save() 才有完整字段可写
                             mychat::SessionManager::instance().setEndpoint(
                                 QStringLiteral("127.0.0.1"), server.port());
                             socket.login(static_cast<std::uint64_t>(
                                              QString::fromLatin1(kSelfUid).toULongLong()),
                                          QStringLiteral("fake-token"));
                         });

    QObject::connect(&socket, &mychat::ChatSocket::connectFailed, &app, [&](const QString& why)
                         {
                             fail(QStringLiteral("连接假服务器失败: %1").arg(why));
                             app.exit(3);
                         });

    socket.connectToServer(QStringLiteral("127.0.0.1"), server.port());

    return app.exec();
}
