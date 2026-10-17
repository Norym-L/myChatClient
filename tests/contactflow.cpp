// 联系人 / 好友链路自检 —— 无 GUI，直接驱动 LoginController + ContactController。
//
// 服务端**已实现**的三条 TCP 消息在这里全覆盖：
//   搜索用户       SearchUserReq/Rsp
//   发送好友申请   AddFriendReq/Rsp
//   好友申请列表   SyncFriendRequestReq/Rsp
//
// 分两段跑，第二段是关键证据：A 发出的申请，要能被 B 在「好友申请列表」里看到。
//   A 登录 → 搜索 B → 发送好友申请 → 退出
//   B 登录 → 拉取好友申请列表 → 应能找到来自 A 的那条
//
// 用法：
//   qtClientContactTest <A_uid> <A_pwd> <B_uid> <B_pwd> [verifyInfo]
// 例：
//   qtClientContactTest 666674 NewPass@1 666672 Test@12345 "hello"
//
// 退出码：0 全部通过；2 参数错误；3 中途失败；4 超时；5 第二段未收到来自 A 的申请。

#include <QCoreApplication>
#include <QLatin1Char>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <cstdio>
#include <functional>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "ContactController.h"
#include "HttpClient.h"
#include "LoginController.h"
#include "logger.h"

namespace
{
constexpr int kTimeoutMs = 40000;

// Qt 6 里 qPrintable() == toLocal8Bit()，中文 Windows 上产出 GBK 字节，
// 而下面把控制台切成了 CP_UTF8 → 必乱码。统一显式 toUtf8()，
// 并让 QByteArray 活到写完（toUtf8().constData() 是悬垂指针）。
void writeLine(FILE* stream, const QString& text)
{
    const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
    std::fflush(stream);
}

enum class Step
{
    LoginA,    ///< A 登录
    Search,    ///< 搜索 B
    AddFriend, ///< A 向 B 发好友申请
    LogoutA,
    LoginB,   ///< B 登录
    SyncList, ///< B 拉取好友申请列表
    Done
};
} // namespace

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif

    mychat::log::init();

    QCoreApplication app(argc, argv);

    if (argc < 5)
    {
        writeLine(stderr,
                  QStringLiteral("用法: %1 <A_uid> <A_pwd> <B_uid> <B_pwd> [verifyInfo]")
                 .arg(QString::fromUtf8(argv[0])));
        return 2;
    }

    const QString uid_a = QString::fromUtf8(argv[1]);
    const QString pwd_a = QString::fromUtf8(argv[2]);
    const QString uid_b = QString::fromUtf8(argv[3]);
    const QString pwd_b = QString::fromUtf8(argv[4]);
    const QString verify_info = argc >= 6 ? QString::fromUtf8(argv[5]) : QStringLiteral("你好，加个好友");

    mychat::HttpClient::instance().setBaseUrl(QStringLiteral("http://127.0.0.1:8080"));

    LoginController login;
    ContactController contact;

    int exit_code = 3;
    Step step = Step::LoginA;

    /*
     * 只有「B 主动回拉之后」的那份列表才配当判据。
     *
     * 因为 A 发申请成功后，控制器会**立刻在本地补一条**「已发出」记录（防重复发送的闸门），
     * 那条也会触发 applyListChanged —— 它的 fromUid 就是 A，正好能骗过下面的判据。
     * 也就是说：没有这个开关，即使服务端一条都没存，本用例也会打 [OK]。
     * 之前就是这样"通过"的（2026-10-17 发现）。
     */
    bool awaiting_server_list = false;

    writeLine(stdout, QStringLiteral("A = %1    B = %2").arg(uid_a, uid_b));

    // ── 状态推进 ──────────────────────────────────────────────
    // LogoutA 这一档要在回调里再次调用自己，所以不能用 auto（类型无法自推导），
    // 必须先声明成 std::function 再赋值。
    std::function<void()> advance;
    advance = [&]()
        {
            switch (step)
            {
                case Step::LoginA:
                    writeLine(stdout, QStringLiteral("\n── 阶段 1：A 登录 ──"));
                    step = Step::Search;
                    login.login(uid_a, pwd_a);
                    break;

                case Step::Search:
                    writeLine(stdout, QStringLiteral("\n── 阶段 2：搜索 B ──"));
                    // CONTACT_SEARCH_ONLY=1 时只跑搜索（无副作用，用于验证多结果解析）
                    if (qEnvironmentVariableIsSet("CONTACT_SEARCH_ONLY"))
                    {
                        step = Step::Done;
                        contact.searchUser(uid_b);
                        break;
                    }
                    step = Step::AddFriend;
                    contact.searchUser(uid_b);
                    break;

                case Step::AddFriend:
                    // 服务端可能返回多个命中（按名字搜索时同名用户都会带上），
                    // 优先取与 uid_b 精确匹配的那条，否则取第一条
                    writeLine(stdout, QStringLiteral("\n── 阶段 3：A 向 B 发好友申请 ──"));
                    step = Step::LogoutA;
                    {
                        QString target;
                        const QVariantList results = contact.searchResults();
                        for (const QVariant& item: results)
                        {
                            const QVariantMap map = item.toMap();
                            const QString uid = map.value(QStringLiteral("uid")).toString();
                            if (target.isEmpty())
                                target = uid;
                            if (uid == uid_b)
                            {
                                target = uid;
                                break;
                            }
                        }
                        // 第 4 个参数是对方的显示名，只用于控制器本地补的那条「已发出」记录；
                        // 自检程序手里只有 uid，传空串即可（列表里会显示成只有 UID）
                        contact.sendFriendRequest(target, verify_info, QStringLiteral(""),
                                                  QStringLiteral(""));
                    }
                    break;

                case Step::LogoutA:
                    writeLine(stdout, QStringLiteral("\n── 阶段 4：A 退出 ──"));
                    step = Step::LoginB;
                    login.logout();
                    // logout 没有完成信号，短延后进入下一段
                    QTimer::singleShot(300, &app, [&]() { advance(); });
                    break;

                case Step::LoginB:
                    writeLine(stdout, QStringLiteral("\n── 阶段 5：B 登录 ──"));
                    step = Step::SyncList;
                    login.login(uid_b, pwd_b);
                    break;

                case Step::SyncList:
                    writeLine(stdout, QStringLiteral("\n── 阶段 6：B 拉取好友申请 ──"));
                    step = Step::Done;
                    // 从这里往后的列表才是服务端给的
                    awaiting_server_list = true;
                    contact.refreshApplyList();
                    break;

                case Step::Done:
                    break;
            }
        };

    // ── 登录结果 ──────────────────────────────────────────────
    QObject::connect(&login, &LoginController::loginSucceeded, &app, [&]()
                         {
                             writeLine(stdout, QStringLiteral("[login] 登录成功: %1").arg(login.userLabel()));
                             advance();
                         });

    QObject::connect(&login, &LoginController::loginFailed,
                     [&exit_code](const QString& reason)
                         {
                             writeLine(stderr, QStringLiteral("\n[FAIL] 登录失败: %1").arg(reason));
                             exit_code = 3;
                             QCoreApplication::quit();
                         });

    // ── 搜索结果 ──────────────────────────────────────────────
    QObject::connect(&contact, &ContactController::searchFinished,
                     [&](const bool found, const int error)
                         {
                             if (!found)
                             {
                                 writeLine(stderr,
                                           QStringLiteral("\n[FAIL] 搜索失败, error = %1").arg(error));
                                 exit_code = 3;
                                 QCoreApplication::quit();
                                 return;
                             }
                             const QVariantList results = contact.searchResults();
                             QStringList hits;
                             for (const QVariant& item: results)
                             {
                                 const QVariantMap map = item.toMap();
                                 hits.append(QStringLiteral("UID %1  昵称 %2")
                                    .arg(map.value(QStringLiteral("uid")).toString(),
                                         map.value(QStringLiteral("name")).toString()));
                             }
                             writeLine(stdout, QStringLiteral("[search] 命中 %1 个: %2")
                                              .arg(results.size()).arg(hits.join(QStringLiteral(" | "))));
                             if (qEnvironmentVariableIsSet("CONTACT_SEARCH_ONLY"))
                             {
                                 // 只跑搜索：到这里就是终点，正常退出
                                 exit_code = found ? 0 : 3;
                                 QCoreApplication::quit();
                                 return;
                             }
                             advance();
                         });

    // ── 好友申请结果 ──────────────────────────────────────────
    QObject::connect(&contact, &ContactController::friendRequestSent,
                     [&](const bool ok, const int error)
                         {
                             if (!ok)
                             {
                                 // 1016「已是好友」/ 1018「检查收到的请求」属于预期内的重复发送，
                                 // 不算失败 —— 第二次跑这个测试就会遇到
                                 writeLine(stdout,
                                           QStringLiteral("[add] 未发送(可能已发过): error=%1 %2")
                                          .arg(error)
                                          .arg(mychat::protocol::errorToTip(error)));
                             }
                             else
                             {
                                 writeLine(stdout, QStringLiteral("[add] 好友申请已发送"));
                             }
                             advance();
                         });

    // ── 申请列表：最终判据 ────────────────────────────────────
    QObject::connect(&contact, &ContactController::applyListChanged, &app, [&]()
                         {
                             if (!awaiting_server_list)
                             {
                                 // A 发完申请后本地补的那条，不是服务端数据 —— 不能当判据
                                 writeLine(stdout, QStringLiteral(
                                               "[apply] 本地补记的回声（%1 条），忽略")
                                          .arg(contact.applyList().size()));
                                 return;
                             }

                             const QVariantList list = contact.applyList();
                             writeLine(stdout, QStringLiteral("[apply] 共 %1 条（来自服务端回包）")
                                      .arg(list.size()));

                             bool found_from_a = false;
                             for (const QVariant& item: list)
                             {
                                 const QVariantMap map = item.toMap();
                                 const QString from = map.value(QStringLiteral("fromUid")).toString();
                                 writeLine(stdout, QStringLiteral("   · from=%1  name=%2  verify=%3  state=%4")
                                          .arg(from,
                                               map.value(QStringLiteral("name")).toString(),
                                               map.value(QStringLiteral("verifyInfo")).toString(),
                                               map.value(QStringLiteral("state")).toString()));

                                 if (from == uid_a)
                                     found_from_a = true;
                             }

                             if (found_from_a)
                             {
                                 writeLine(stdout, QStringLiteral("\n[OK] B 收到了来自 A(%1) 的好友申请").arg(uid_a));
                                 exit_code = 0;
                             }
                             else
                             {
                                 writeLine(stderr, QStringLiteral("\n[FAIL] B 的申请列表里没有来自 A(%1) 的记录").arg(uid_a));
                                 exit_code = 5;
                             }
                             QCoreApplication::quit();
                         });

    // ── 提示 ──────────────────────────────────────────────────
    QObject::connect(&contact, &ContactController::tipChanged, [&contact]()
                         {
                             writeLine(stderr, QStringLiteral("[tip%1] %2")
                                      .arg(contact.tipIsError() ? QStringLiteral("/err") : QString(),
                                           contact.tip()));
                         });

    QTimer::singleShot(kTimeoutMs, &app, [&exit_code]()
                           {
                               writeLine(stderr, QStringLiteral("\n[FAIL] 超时（%1 ms 内未完成）").arg(kTimeoutMs));
                               exit_code = 4;
                               QCoreApplication::quit();
                           });

    advance();

    app.exec();

    writeLine(stdout, QStringLiteral("[exit] %1").arg(exit_code));
    return exit_code;
}
