#ifndef CHATCLIENT_USERDATA_H
#define CHATCLIENT_USERDATA_H

#include <cstdint>
#include <QString>
#include <memory>

enum class Sex : unsigned char
{
	MALE,
	FEMALE,
	UNKNOWN
};

struct SearchInfo
{
	explicit SearchInfo(const uint64_t uid, const QString& userName, const QString& nick, const QString& desc,
	                    const Sex sex)
		: uid(uid), userName(userName), nick(nick), desc(desc), sex(sex)
	{}

	uint64_t uid; // 用户id
	QString userName; // 用户名
	QString nick; // 昵称, 用户自定义的备注
	QString desc; // 描述
	Sex sex; // 性别
};

struct AddFriendApply // 添加好友申请
{
	explicit AddFriendApply(const std::uint64_t from_uid, const QString& name, const QString& desc)
		: from_uid(from_uid), name(name), desc(desc)
	{}

	std::uint64_t from_uid;
	QString name;
	QString desc;
};

struct ApplyInfo
{
	explicit ApplyInfo(const std::uint64_t uid, const QString& name, const QString& desc, const QString& icon, const QString& nick, const Sex sex, const int status)
		: uid(uid), name(name), desc(desc), icon(icon), nick(nick), sex(sex), status(status)
	{}

	void setIcon(const QString& head) { icon = head; }

	std::uint64_t uid;
	QString name;
	QString desc;
	QString icon;
	QString nick;
	Sex sex;
	int status; // 是否接受
};

struct AuthInfo
{
	explicit AuthInfo(const std::uint64_t uid, const QString& name, const QString& icon, const QString& nick, const Sex sex)
		: uid(uid), name(name), icon(icon), nick(nick), sex(sex)
	{}

	std::uint64_t uid;
	QString name;
	QString icon;
	QString nick;
	Sex sex;
};

struct AuthRsp
{
	explicit AuthRsp(const std::uint64_t uid, const QString& name, const QString& icon, const QString& nick, const Sex sex)
		: uid(uid), name(name), icon(icon), nick(nick), sex(sex)
	{}

	std::uint64_t uid;
	QString name;
	QString icon;
	QString nick;
	Sex sex;
};

struct UserInfo
{
	explicit UserInfo(const std::uint64_t uid, const QString& name, const QString& icon, const QString& nick, const Sex sex = Sex::UNKNOWN)
		: uid(uid), name(name), icon(icon), nick(nick), sex(sex)
	{}

	explicit UserInfo(const std::shared_ptr<AuthInfo>& auth)
		: uid(auth->uid), name(auth->name), icon(auth->icon), nick(auth->nick), sex(auth->sex)
	{}

	explicit UserInfo(const std::shared_ptr<AuthRsp>& auth)
		: uid(auth->uid), name(auth->name), icon(auth->icon), nick(auth->nick), sex(auth->sex)
	{}

	std::uint64_t uid;
	QString name;
	QString icon;
	QString nick;
	Sex sex;
};

#endif //CHATCLIENT_USERDATA_H
