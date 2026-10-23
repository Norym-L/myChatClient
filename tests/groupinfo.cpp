// 群资料链路自检（1043–1046）—— 自带假 ChatServer，不需要真实服务端。
//
// 服务端 2026-10-23（7338a5c）新增：
//   1043 / 1044  获取群资料（请求字段叫 `gid`，不是 group_id）
//   1045 / 1046  修改群资料（name / avatar / announcement 都可选；
//                缺省或 null = 不改，空串 = 清空；数据库异常时回包**没有 error**）
//
// 这些约定在真实服务端上不好复现（要造数据库异常、要建群），所以用假服务器钉住：
//   1. 1043 只发 gid，不发 group_id；
//   2. 1044 的 maxMb / mbCnt / createAt 是字符串，解析成数字；
//   3. 1045 只带调用方显式给的可选字段（不改头像就不发 avatar）；
//   4. 1046 成功后要强制重拉 1043（1046 不回新资料）；
//   5. 1046 缺 error 字段必须按失败处理，不能当成功。
//
// 用法：qtClientGroupInfoTest
// 退出码：0 全部通过；3 断言失败；4 整体超时；5 起不了本地监听。

#include <QCoreApplication>
#include <QDir>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTimer>
#include <QVariantMap>

#include <cstdint>
#include <cstdio>
#include <functional>

#include "ChatController.h"
#include "ChatSocket.h"
#include "GroupController.h"
#include "protocol.h"

namespace
{
constexpr std::uint16_t kLoginReq = 1001;
constexpr std::uint16_t kLoginRsp = 1002;
constexpr std::uint16_t kSyncMsgReq = 1023;
constexpr std::uint16_t kSyncMsgRsp = 1024;
constexpr std::uint16_t kGroupInfoReq = 1043;
constexpr std::uint16_t kGroupInfoRsp = 1044;
constexpr std::uint16_t kGroupInfoChangeReq = 1045;
constexpr std::uint16_t kGroupInfoChangeRsp = 1046;

const char* kGid = "20001";
const char* kSelfUid = "666672";

int g_failures = 0;

void writeLine(FILE* stream, const QString& text)
{
    const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
    std::fflush(stream);
}

void check(const bool condition, const QString& what)
{
    if (condition)
    {
        writeLine(stdout, QStringLiteral("  [OK] ") + what);
        return;
    }

    ++g_failures;
    writeLine(stdout, QStringLiteral("  [FAIL] ") + what);
}

QJsonObject parse(const QByteArray& body)
{
    const QJsonDocument document = QJsonDocument::fromJson(body);
    return document.isObject() ? document.object() : QJsonObject{};
}

/// 假 ChatServer：帧 = 2 字节 msgid + 2 字节 body 长度（大端）+ JSON
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

    std::function<void(std::uint16_t, const QJsonObject&)> onFrame;
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

    [[nodiscard]] QJsonObject lastOf(const std::uint16_t id) const
    {
        for (auto it = frames.crbegin(); it != frames.crend(); ++it)
        {
            if (it->first == id)
                return parse(it->second);
        }
        return {};
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
                onFrame(id, parse(body));
        }
    }

    QTcpServer m_server;
    QTcpSocket* m_peer = nullptr;
    QByteArray m_buffer;
};

QByteArray compact(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qtClientGroupInfoTest"));

    // 数据目录放 exe 旁边（%TEMP% 在受限环境里可能不可写）
    const QString data_path = QDir(QCoreApplication::applicationDirPath())
                                  .filePath(QStringLiteral("test-data/mychat-groupinfo-test"));
    QDir(data_path).removeRecursively();
    if (!QDir().mkpath(data_path))
    {
        writeLine(stderr, QStringLiteral("[FAIL] 建不了临时数据目录: %1").arg(data_path));
        return 3;
    }
    qputenv("MYCHAT_DATA_DIR", data_path.toUtf8());

    FakeServer server;
    if (!server.listen())
    {
        writeLine(stderr, QStringLiteral("[FAIL] 假服务器起不了监听"));
        return 5;
    }

    ChatController chat;
    GroupController group;
    auto& socket = mychat::ChatSocket::instance();

    QTimer step_timer;
    step_timer.setSingleShot(true);
    QObject::connect(&step_timer, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 某一步超时"));
                             app.exit(4);
                         });

    QTimer overall;
    overall.setSingleShot(true);
    QObject::connect(&overall, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 整体超时"));
                             app.exit(4);
                         });

    bool info_stage_done = false;
    bool change1_done = false;
    bool refresh_done = false;
    bool change2_done = false;

    auto finish = [&]()
    {
        step_timer.stop();
        overall.stop();
        writeLine(stdout, g_failures == 0
                  ? QStringLiteral("[PASS] 全部通过")
                  : QStringLiteral("[FAIL] %1 项失败").arg(g_failures));
        app.exit(g_failures == 0 ? 0 : 3);
    };

    /*
     * 假服务端行为：
     *   1043 → 第一次回「鲸鲸原群 / 旧公告 / 3 人」，之后回改后的资料；
     *   1045 → 第一次 {error:0}，第二次只回 msgid（模拟数据库异常）；
     *   1023 → 空拉取（登录后客户端会主动拉一次）。
     */
    server.onFrame = [&](const std::uint16_t id, const QJsonObject& body)
    {
        const QJsonValue msgid = body.value(QStringLiteral("msgid"));

        switch (id)
        {
        case kLoginReq:
            server.reply(kLoginRsp, QByteArrayLiteral(
                             R"({"error":0,"uid":"666672","token":"fake","name":"自检","has_pending":"0"})"));
            break;

        case kSyncMsgReq:
            server.reply(kSyncMsgRsp, QByteArrayLiteral(R"({"error":0,"msgid":null})"));
            break;

        case kGroupInfoReq:
        {
            const bool first = server.countOf(kGroupInfoReq) <= 1;

            QJsonObject reply;
            reply.insert(QStringLiteral("error"), 0);
            reply.insert(QStringLiteral("msgid"), msgid);
            reply.insert(QStringLiteral("name"),
                         first ? QStringLiteral("鲸鲸原群") : QStringLiteral("鲸鲸新群"));
            reply.insert(QStringLiteral("avatar"), QString());
            reply.insert(QStringLiteral("owner_uid"), QString::fromLatin1(kSelfUid));
            reply.insert(QStringLiteral("announcement"),
                         first ? QStringLiteral("旧公告") : QStringLiteral("新公告"));
            // ⚠ 真实服务端这三个字段都是字符串
            reply.insert(QStringLiteral("maxMb"), QStringLiteral("500"));
            reply.insert(QStringLiteral("mbCnt"), QStringLiteral("3"));
            reply.insert(QStringLiteral("createAt"), QStringLiteral("1734567890123"));
            server.reply(kGroupInfoRsp, compact(reply));
            break;
        }

        case kGroupInfoChangeReq:
        {
            const bool first = server.countOf(kGroupInfoChangeReq) <= 1;

            QJsonObject reply;
            if (first)
                reply.insert(QStringLiteral("error"), 0);
            // 第二次故意不写 error：存储过程 -10/-11 时服务端就是这样回的
            reply.insert(QStringLiteral("msgid"), msgid);
            server.reply(kGroupInfoChangeRsp, compact(reply));
            break;
        }

        default:
            break;
        }
    };

    QObject::connect(&socket, &mychat::ChatSocket::connected, &app, [&]()
                         {
                             writeLine(stdout, QStringLiteral("[step] 已连接假服务器，登录"));
                             step_timer.start(7000);
                             socket.login(666672, QStringLiteral("fake-token"));
                         });

    QObject::connect(&socket, &mychat::ChatSocket::loginSucceeded, &app,
                     [&](std::uint64_t, const QString&, const QString&)
                         {
                             // 等 ChatController 的登录槽跑完（它会发 1023）
                             QTimer::singleShot(150, &app, [&]()
                                                    {
                                                        writeLine(stdout, QStringLiteral(
                                                            "[step] 拉群资料（1043 → 1044）"));
                                                        step_timer.start(7000);
                                                        group.loadGroupInfo(QString::fromLatin1(kGid));
                                                    });
                         });

    QObject::connect(&group, &GroupController::groupInfoChanged, &app, [&]()
                         {
                             const QVariantMap info = group.groupInfo();
                             if (info.isEmpty())
                                 return; // loadGroupInfo 切 gid 时先清空一次

                             if (!info_stage_done)
                             {
                                 info_stage_done = true;
                                 step_timer.start(7000);

                                 const QJsonObject request = server.lastOf(kGroupInfoReq);
                                 check(request.value(QStringLiteral("gid")).toString()
                                           == QLatin1String(kGid),
                                       QStringLiteral("1043 的字段名是 gid"));
                                 check(!request.contains(QStringLiteral("group_id")),
                                       QStringLiteral("1043 不发 group_id"));

                                 check(info.value(QStringLiteral("name")).toString()
                                           == QStringLiteral("鲸鲸原群"),
                                       QStringLiteral("1044 解析出群名"));
                                 check(info.value(QStringLiteral("announcement")).toString()
                                           == QStringLiteral("旧公告"),
                                       QStringLiteral("1044 解析出公告"));
                                 check(info.value(QStringLiteral("ownerUid")).toString()
                                           == QLatin1String(kSelfUid),
                                       QStringLiteral("1044 解析出 owner_uid"));
                                 check(info.value(QStringLiteral("maxMembers")).toInt() == 500,
                                       QStringLiteral("字符串 maxMb 转成数字 500"));
                                 check(info.value(QStringLiteral("memberCount")).toInt() == 3,
                                       QStringLiteral("字符串 mbCnt 转成数字 3"));
                                 check(info.value(QStringLiteral("createAt")).toLongLong()
                                           == 1734567890123LL,
                                       QStringLiteral("字符串 createAt 转成毫秒"));

                                 writeLine(stdout, QStringLiteral(
                                     "[step] 改群资料（1045 → 1046 → 重拉 1043）"));
                                 group.changeGroupInfo(QString::fromLatin1(kGid),
                                                       QStringLiteral("鲸鲸新群"),
                                                       QStringLiteral("群公告：请勿刷屏"));
                                 return;
                             }

                             if (change1_done && !refresh_done)
                             {
                                 refresh_done = true;
                                 step_timer.start(7000);

                                 check(info.value(QStringLiteral("name")).toString()
                                           == QStringLiteral("鲸鲸新群"),
                                       QStringLiteral("1046 成功后强制重拉 1043，拿到新群名"));
                                 check(info.value(QStringLiteral("announcement")).toString()
                                           == QStringLiteral("新公告"),
                                       QStringLiteral("重拉后的公告也是新值"));
                                 check(server.countOf(kGroupInfoReq) == 2,
                                       QStringLiteral("1046 成功后确实又发了一次 1043"));

                                 writeLine(stdout, QStringLiteral(
                                     "[step] 清空公告 + 缺 error 的 1046"));
                                 group.changeGroupInfo(QString::fromLatin1(kGid),
                                                       QStringLiteral("鲸鲸新群"),
                                                       QString());
                             }
                         });

    QObject::connect(&socket, &mychat::ChatSocket::groupInfoChangeResult, &app,
                     [&](const bool ok, const int error, const QString& gid)
                         {
                             check(gid == QLatin1String(kGid),
                                   QStringLiteral("1046 回包带回 gid"));

                             if (!change1_done)
                             {
                                 change1_done = true;
                                 step_timer.start(7000);

                                 check(ok && error == 0,
                                       QStringLiteral("1045 修改群资料成功"));

                                 const QJsonObject request = server.lastOf(kGroupInfoChangeReq);
                                 check(request.value(QStringLiteral("gid")).toString()
                                           == QLatin1String(kGid),
                                       QStringLiteral("1045 的字段名是 gid"));
                                 check(request.value(QStringLiteral("name")).toString()
                                           == QStringLiteral("鲸鲸新群"),
                                       QStringLiteral("1045 带新群名"));
                                 check(request.value(QStringLiteral("announcement")).toString()
                                           == QStringLiteral("群公告：请勿刷屏"),
                                       QStringLiteral("1045 带新公告"));
                                 check(!request.contains(QStringLiteral("avatar")),
                                       QStringLiteral("不改头像时不发 avatar（缺省=保留）"));
                                 return;
                             }

                             if (refresh_done && !change2_done)
                             {
                                 change2_done = true;
                                 step_timer.start(7000);

                                 check(!ok, QStringLiteral("1046 缺 error 字段按失败处理"));
                                 check(error == static_cast<int>(
                                           mychat::protocol::ErrorCode::JsonError),
                                       QStringLiteral("缺 error 归为 JsonError"));
                                 check(group.tip().contains(QStringLiteral("修改群资料失败")),
                                       QStringLiteral("失败提示写进 tip"));

                                 const QJsonObject request = server.lastOf(kGroupInfoChangeReq);
                                 check(request.value(QStringLiteral("announcement")).isString()
                                           && request.value(QStringLiteral("announcement"))
                                                  .toString().isEmpty(),
                                       QStringLiteral("空公告按空串发出（= 清空）"));

                                 finish();
                             }
                         });

    step_timer.start(7000);
    overall.start(30000);

    QTimer::singleShot(0, &app, [&]()
                           {
                               socket.connectToServer(QStringLiteral("127.0.0.1"), server.port());
                           });

    const int result = app.exec();
    writeLine(stdout, QStringLiteral("[exit] %1").arg(result));
    return result;
}
