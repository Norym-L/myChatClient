#ifndef CHATCLIENT_USERDATA_H
#define CHATCLIENT_USERDATA_H
#include <cstdint>

#include <QString>

class SearchInfo
{
public:
    enum class Sex
    {
        MALE,
        FEMALE,
        UNKNOWN
    };

    explicit SearchInfo(const uint64_t uid, const QString& userName, const QString& nick, const QString& desc, const Sex sex)
        : uid(uid), userName(userName), nick(nick), desc(desc), sex(sex)
    {}

    uint64_t uid; // 用户id
    QString userName; // 用户名
    QString nick; // 昵称, 用户自定义的备注
    QString desc; // 描述
    Sex sex; // 性别
};

#endif //CHATCLIENT_USERDATA_H
