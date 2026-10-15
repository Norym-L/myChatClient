#include "AccountValidator.h"

#include <QRegularExpression>

namespace
{
QString matchOr(const QString& text, const QRegularExpression& re, const QString& error_tip,
                const QString& empty_tip)
{
    if (text.isEmpty())
        return empty_tip;
    if (!re.match(text).hasMatch())
        return error_tip;
    return {};
}
} // namespace

QString AccountValidator::checkUid(const QString& text)
{
    // 首位不能是 0：uid 是自增正整数，前导 0 没有意义且易与八进制混淆
    static const QRegularExpression re(QStringLiteral(R"(^[1-9][0-9]{3,}$)"));
    return matchOr(text, re, QStringLiteral("账号格式错误：需为 4 位以上数字，且首位不能是 0"),
                   QStringLiteral("请输入账号"));
}

QString AccountValidator::checkEmail(const QString& text)
{
    static const QRegularExpression re(
        QStringLiteral(R"(^[A-Za-z0-9._%+'-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)+$)"));
    return matchOr(text, re, QStringLiteral("邮箱格式错误"), QStringLiteral("请输入邮箱"));
}

QString AccountValidator::checkPassword(const QString& text)
{
    static const QRegularExpression re(QStringLiteral(R"(^[a-zA-Z0-9!@#$%^&*]{6,15}$)"));
    return matchOr(text, re, QStringLiteral("密码格式错误：6~15 位，仅限字母、数字与 !@#$%^&* 符号"),
                   QStringLiteral("请输入密码"));
}

QString AccountValidator::checkNickname(const QString& text)
{
    // 用排除法而不是白名单：昵称要支持中文，列全 Unicode 类别不现实也容易漏。
    // 这里只挡掉真正会出问题的字符 —— 空白（破坏协议字段）与 @（与邮箱语义混淆）。
    static const QRegularExpression re(QStringLiteral(R"(^[^\s@]{2,16}$)"));
    return matchOr(text, re, QStringLiteral("昵称需为 2~16 位，且不能包含空格或 @"),
                   QStringLiteral("请输入昵称"));
}

QString AccountValidator::checkVerifyCode(const QString& text)
{
    // 验证码为 6 位数字或字母
    static const QRegularExpression re(QStringLiteral(R"(^[a-zA-Z0-9]{6}$)"));
    return matchOr(text, re, QStringLiteral("验证码为 6 位数字或字母"), QStringLiteral("请输入验证码"));
}
