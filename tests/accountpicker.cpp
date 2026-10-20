// 账号卡片自检 —— 「记住多个账号 → 点卡片免密进入」这条链路。
//
// 钉住六件事：
//   1. 登录成功后凭据（uid/name/token/host/port）真的落进 Accounts/<uid>；
//   2. 两个账号先后登录 → 列表 2 条、最后一个排最前、lastUid 指向它；
//   3. 内存清空（模拟重启）后 loginWithAccount(A) 能凭 A 的 token 免密进入；
//   4. token 被顶掉（1012）→ 只清 A 的 token，**卡片保留**（还能用密码登）；
//   5. 用户主动退出登录 → 只清当前账号的 token，其它卡片不受影响；
//   6. 旧的单账号结构（[Session] uid/token/...）会自动迁移成一张卡片。
//
// 用构建目录下的隔离文件，不碰用户真实的 session.ini。
//
// 用法（需要真实服务端 + 两个账号）：
//   qtClientAccountPickerTest <uidA> <pwdA> <uidB> <pwdB> [gateUrl]
//
// 退出码：0 通过；2 参数错误；3 断言失败；4 超时。

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLatin1Char>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include <cstdio>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

#include "ChatSocket.h"
#include "HttpClient.h"
#include "LoginController.h"
#include "SessionManager.h"
#include "logger.h"

namespace
{
constexpr int kTimeoutMs = 90000;

void writeLine(FILE* stream, const QString& text)
{
    const QByteArray bytes = (text + QLatin1Char('\n')).toUtf8();
    std::fwrite(bytes.constData(), 1, static_cast<std::size_t>(bytes.size()), stream);
    std::fflush(stream);
}

QSettings openStore()
{
    // 与产品代码读同一份文件：storagePath() 已经考虑了 MYCHAT_SESSION_FILE
    return QSettings(mychat::SessionManager::storagePath(), QSettings::IniFormat);
}

bool storeHasAccount(const QString& uid)
{
    QSettings settings = openStore();
    settings.beginGroup(QStringLiteral("Accounts"));
    return settings.childGroups().contains(uid);
}

QString storedToken(const QString& uid)
{
    QSettings settings = openStore();
    return settings.value(QStringLiteral("Accounts/") + uid + QStringLiteral("/token")).toString();
}

void setStoredToken(const QString& uid, const QString& token)
{
    QSettings settings = openStore();
    settings.setValue(QStringLiteral("Accounts/") + uid + QStringLiteral("/token"), token);
    settings.sync();
}

QString storedLastUid()
{
    QSettings settings = openStore();
    return settings.value(QStringLiteral("Session/lastUid")).toString();
}

bool storedSkipAuto()
{
    QSettings settings = openStore();
    return settings.value(QStringLiteral("Session/skipAuto"), false).toBool();
}

/// 从 LoginController.accounts() 里取某一条（找不到返回空 map）
QVariantMap accountEntry(const QVariantList& list, const QString& uid)
{
    for (const QVariant& entry: list)
    {
        const QVariantMap item = entry.toMap();
        if (item.value(QStringLiteral("uid")).toString() == uid)
            return item;
    }
    return {};
}
} // namespace

int main(int argc, char* argv[])
{
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif

    mychat::log::init();

    QCoreApplication app(argc, argv);

    /*
     * 会话存储指到**构建目录**下的隔离文件。
     *
     * 走的是产品代码自己的开关（MYCHAT_SESSION_FILE），测的就是应用真正会用的
     * 那条路径。放 exe 旁边而不是 %TEMP%：构建目录一定可写，而 %TEMP% 在受限
     * 环境下会静默写失败，症状像"保存成功但读回来是空的"。
     */
    const QString store_file = QCoreApplication::applicationDirPath()
                               + QStringLiteral("/test-data/accountpicker/session.ini");
    if (!QDir().mkpath(QFileInfo(store_file).absolutePath()))
    {
        writeLine(stderr, QStringLiteral("[FAIL] 建不了测试目录: %1").arg(store_file));
        return 2;
    }
    QFile::remove(store_file);
    qputenv("MYCHAT_SESSION_FILE", store_file.toUtf8());

    if (argc < 5)
    {
        writeLine(stderr, QStringLiteral("用法: %1 <uidA> <pwdA> <uidB> <pwdB> [gateUrl]")
                 .arg(QString::fromUtf8(argv[0])));
        return 2;
    }

    const QString uid_a = QString::fromUtf8(argv[1]);
    const QString pwd_a = QString::fromUtf8(argv[2]);
    const QString uid_b = QString::fromUtf8(argv[3]);
    const QString pwd_b = QString::fromUtf8(argv[4]);
    const QString gate = argc >= 6 ? QString::fromUtf8(argv[5])
                                   : QStringLiteral("http://127.0.0.1:8080");

    mychat::HttpClient::instance().setBaseUrl(gate);

    LoginController login;
    mychat::SessionManager::instance().clear();

    int fail_count = 0;
    const auto check = [&](const bool ok, const QString& what)
        {
            if (ok)
            {
                writeLine(stdout, QStringLiteral("[OK] %1").arg(what));
                return;
            }
            writeLine(stderr, QStringLiteral("[FAIL] %1").arg(what));
            ++fail_count;
        };

    writeLine(stdout, QStringLiteral("A = %1    B = %2    gate = %3\nstore = %4")
              .arg(uid_a, uid_b, gate, store_file));

    QTimer overall;
    overall.setSingleShot(true);
    QObject::connect(&overall, &QTimer::timeout, &app, [&]()
                         {
                             writeLine(stderr, QStringLiteral("[FAIL] 整体超时"));
                             app.exit(4);
                         });
    overall.start(kTimeoutMs);

    int stage = 0;
    /// 哪几步是"故意让它失败"的 —— 那些阶段的 loginFailed 是预期结果
    bool expecting_failure = false;

    QObject::connect(&login, &LoginController::loginFailed, &app, [&](const QString& why)
                         {
                             if (!expecting_failure)
                             {
                                 writeLine(stderr, QStringLiteral("[FAIL] 登录失败: %1").arg(why));
                                 app.exit(3);
                                 return;
                             }

                             expecting_failure = false;
                             writeLine(stdout, QStringLiteral("   服务端拒绝: %1").arg(why));

                             // ── 篡改 token 之后 ──
                             check(storeHasAccount(uid_a),
                                   QStringLiteral("凭据失效后 A 的卡片还在（只是不能再免密）"));
                             check(storedToken(uid_a).isEmpty(),
                                   QStringLiteral("A 的 token 已被清掉"));

                             const QVariantList list = login.accounts();
                             check(accountEntry(list, uid_a)
                                       .value(QStringLiteral("hasToken")).toBool() == false,
                                   QStringLiteral("UI 侧 A 卡片标记为「不免密」"));

                             // ── 移除卡片 ──
                             login.forgetAccount(uid_a);
                             check(login.accounts().size() == 1,
                                   QStringLiteral("移除 A 后只剩 B 一张卡片"));
                             check(!storeHasAccount(uid_a),
                                   QStringLiteral("磁盘上 A 的记录也没了"));

                             /*
                              * ── 退出登录不动凭据 ──
                              *
                              * 此刻内存里的当前账号还是 A（刚被移除），先真的登进 B
                              * 再退出，才能验"退出只断连接、不删凭据"。
                              */
                             stage = 4;
                             writeLine(stdout, QStringLiteral("\n── 免密进入 B，再退出登录 ──"));
                             if (!login.loginWithAccount(uid_b))
                             {
                                 writeLine(stderr, QStringLiteral(
                                               "[FAIL] B 应当还能免密进入"));
                                 ++fail_count;
                                 app.exit(3);
                             }
                         });

    QObject::connect(&login, &LoginController::loginSucceeded, &app, [&]()
                         {
                             const auto& session = mychat::SessionManager::instance();
                             const QString uid_now = QString::number(session.uid());

                             if (stage == 4)
                             {
                                 check(uid_now == uid_b, QStringLiteral("回到 B"));

                                 /*
                                  * 退出登录只断开连接，**凭据要留着** ——
                                  * 用户的诉求是"退出来点一下卡片就能回去，别再输密码"。
                                  * 真要把号忘掉是卡片上的「移除」（forgetAccount）。
                                  */
                                 login.logout();
                                 check(!storedToken(uid_b).isEmpty(),
                                       QStringLiteral("退出登录后 B 的凭据保留"));
                                 check(storeHasAccount(uid_b),
                                       QStringLiteral("B 的卡片也还在"));
                                 check(login.accounts().size() == 1,
                                       QStringLiteral("账号列表没被退出动作清掉"));
                                 check(storedSkipAuto(),
                                       QStringLiteral("退出登录记下「下次别自动登」"));

                                 /*
                                  * 模拟"退出后关掉再打开"：tryAutoLogin 必须停手，
                                  * 但要把上次那个号选好，让用户点一下就回去。
                                  */
                                 check(!login.tryAutoLogin(),
                                       QStringLiteral("主动退出后启动不再自动登录"));
                                 check(login.selectedUid() == uid_b,
                                       QStringLiteral("登录页默认选中上次那个账号"));

                                 stage = 5;
                                 writeLine(stdout, QStringLiteral(
                                               "\n── 退出后点卡片，应当还能免密进 ──"));
                                 if (!login.loginWithAccount(uid_b))
                                 {
                                     writeLine(stderr, QStringLiteral(
                                                   "[FAIL] 退出登录后卡片应当仍可免密进入"));
                                     ++fail_count;
                                     app.exit(3);
                                 }
                                 return;
                             }

                             if (stage == 5)
                             {
                                 check(uid_now == uid_b,
                                       QStringLiteral("退出登录后点卡片，仍然免密进来了"));
                                 check(!storedSkipAuto(),
                                       QStringLiteral("登录成功后「别自动登」的标记被清掉"));

                                 // ── 旧结构迁移 ──
                                 const QString legacy_file =
                                     QCoreApplication::applicationDirPath()
                                     + QStringLiteral("/test-data/accountpicker/legacy.ini");
                                 QFile::remove(legacy_file);
                                 {
                                     QSettings legacy(legacy_file, QSettings::IniFormat);
                                     legacy.setValue(QStringLiteral("Session/uid"),
                                                     QStringLiteral("666001"));
                                     legacy.setValue(QStringLiteral("Session/name"),
                                                     QStringLiteral("legacy"));
                                     legacy.setValue(QStringLiteral("Session/token"),
                                                     QStringLiteral("old-token"));
                                     legacy.setValue(QStringLiteral("Session/host"),
                                                     QStringLiteral("127.0.0.1"));
                                     legacy.setValue(QStringLiteral("Session/port"), 8090);
                                     legacy.sync();
                                 }
                                 qputenv("MYCHAT_SESSION_FILE", legacy_file.toUtf8());
                                 const QList<mychat::SessionManager::Account> migrated =
                                     mychat::SessionManager::instance().accounts();
                                 check(migrated.size() == 1 && migrated.first().uid == 666001,
                                       QStringLiteral("旧的单账号记录迁移成一张卡片"));
                                 check(migrated.first().token == QStringLiteral("old-token"),
                                       QStringLiteral("迁移后 token 没丢"));
                                 {
                                     QSettings legacy(legacy_file, QSettings::IniFormat);
                                     check(legacy.value(QStringLiteral("Session/uid"))
                                               .toString().isEmpty(),
                                           QStringLiteral("旧键已清掉（不会重复迁移）"));
                                     check(legacy.value(QStringLiteral("Session/lastUid"))
                                               .toString() == QStringLiteral("666001"),
                                           QStringLiteral("lastUid 指向迁移过来的账号"));
                                 }

                                 if (fail_count == 0)
                                     writeLine(stdout, QStringLiteral(
                                                   "\n[PASS] 账号卡片的存取 / 免密 / 失效 / 迁移全部通过"));
                                 app.exit(fail_count == 0 ? 0 : 3);
                                 return;
                             }
                         });

    QObject::connect(&login, &LoginController::loginSucceeded, &app, [&]()
                         {
                             const auto& session = mychat::SessionManager::instance();
                             const QString uid_now = QString::number(session.uid());

                             if (stage == 0)
                             {
                                 writeLine(stdout, QStringLiteral("\n── A(%1) 登录成功 ──").arg(uid_a));
                                 check(uid_now == uid_a, QStringLiteral("uid 对得上"));
                                 check(storeHasAccount(uid_a),
                                       QStringLiteral("A 已写进 Accounts/<uid>"));
                                 check(storedLastUid() == uid_a,
                                       QStringLiteral("lastUid 指向 A"));
                                 check(!storedToken(uid_a).isEmpty(),
                                       QStringLiteral("A 的 token 已落盘"));

                                 stage = 1;
                                 mychat::ChatSocket::instance().disconnectFromServer();
                                 mychat::SessionManager::instance().clear();
                                 QTimer::singleShot(300, &app, [&]()
                                                        {
                                                            login.login(uid_b, pwd_b);
                                                        });
                                 return;
                             }

                             if (stage == 1)
                             {
                                 writeLine(stdout, QStringLiteral("\n── B(%1) 登录成功 ──").arg(uid_b));
                                 check(uid_now == uid_b, QStringLiteral("uid 对得上"));

                                 const QVariantList list = login.accounts();
                                 check(list.size() == 2,
                                       QStringLiteral("卡片列表 2 条（A + B）"));
                                 check(list.first().toMap().value(QStringLiteral("uid")).toString()
                                           == uid_b,
                                       QStringLiteral("最近登录的 B 排最前"));
                                 check(accountEntry(list, uid_a)
                                           .value(QStringLiteral("hasToken")).toBool(),
                                       QStringLiteral("A 也带免密凭据"));
                                 check(accountEntry(list, uid_b)
                                           .value(QStringLiteral("isLast")).toBool(),
                                       QStringLiteral("B 打了「上次」标记"));

                                 // ── 模拟重启后点 A 的卡片 ──
                                 stage = 2;
                                 mychat::ChatSocket::instance().disconnectFromServer();
                                 mychat::SessionManager::instance().clear();
                                 QTimer::singleShot(300, &app, [&]()
                                                        {
                                                            writeLine(stdout, QStringLiteral(
                                                                          "\n── 点 A 的卡片（免密）──"));
                                                            check(login.loginWithAccount(uid_a),
                                                                  QStringLiteral(
                                                                      "loginWithAccount 发起了连接"));
                                                            check(login.accounts().size() == 2,
                                                                  QStringLiteral(
                                                                      "只切账号，不动卡片列表"));
                                                        });
                                 return;
                             }

                             if (stage == 2)
                             {
                                 writeLine(stdout, QStringLiteral("\n── 免密进入 A 成功 ──"));
                                 check(uid_now == uid_a,
                                       QStringLiteral("凭 A 的 token 回到了 A"));

                                 // ── 篡改 A 的 token，期望服务端拒绝 ──
                                 stage = 3;
                                 expecting_failure = true;
                                 mychat::ChatSocket::instance().disconnectFromServer();
                                 setStoredToken(uid_a, QStringLiteral("tampered-token-for-test"));
                                 mychat::SessionManager::instance().clear();
                                 writeLine(stdout, QStringLiteral("\n── 篡改 A 的 token，期望被拒 ──"));
                                 if (!login.loginWithAccount(uid_a))
                                 {
                                     writeLine(stderr, QStringLiteral(
                                                   "[FAIL] 有记录时 loginWithAccount 应当发起连接"));
                                     ++fail_count;
                                     app.exit(3);
                                 }
                                 return;
                             }
                         });

    QTimer::singleShot(0, &app, [&]()
                           {
                               login.login(uid_a, pwd_a);
                           });

    const int result = app.exec();
    writeLine(stdout, QStringLiteral("[exit] %1").arg(result));
    return result;
}
