#pragma once

#include <QString>

/**
 * 账号相关的本地校验规则。
 *
 * 集中在这一处的原因很实际：登录、注册、重置密码三个流程都要校验邮箱、
 * 密码，如果各自写一份正则，迟早会出现「本地放行、服务端拒绝」或反之的
 * 错位 —— 这类 bug 表现得很随机，非常难查。
 *
 * 约定：所有函数返回**空串**表示通过，否则返回直接可以显示给用户的提示。
 */
class AccountValidator
{
public:
    /// 账号（uid）：4 位以上数字，首位不为 0
    [[nodiscard]] static QString checkUid(const QString& text);

    /// 邮箱：本地部分 + @ + 域名
    [[nodiscard]] static QString checkEmail(const QString& text);

    /**
     * 密码：6~15 位，仅限字母、数字与 !@#$%^&*
     *
     * 这个字符集合是与服务端对齐的 —— 不包含下划线和点号。
     * 放宽本地规则只会让用户走到「服务端拒绝」这一步，体验更差。
     */
    [[nodiscard]] static QString checkPassword(const QString& text);

    /// 昵称：2~16 位，不含空白字符与 @（@ 会与邮箱语义混淆，空白会破坏协议字段）
    [[nodiscard]] static QString checkNickname(const QString& text);

    /// 验证码：6 位数字
    [[nodiscard]] static QString checkVerifyCode(const QString& text);
};
