#pragma once
#include <QObject>
#include <QString>

#include "singleton.h"

/**
 * @brief UserManager 保存当前登录用户的信息，来源为 /user_login 响应与
 *        ChatServer 登录响应（MESSAGE_CHAT_LOGIN_RESP）中的 uid / name / token
 */
class UserManager :public QObject, public Singleton<UserManager>
{
	friend class Singleton;

	Q_OBJECT

public:
	~UserManager() override;

	[[nodiscard]] int getUid() const { return m_uid; }
	void setUid(const int uid) { m_uid = uid; }

	[[nodiscard]] QString getName() const { return m_name; }
	void setName(const QString& name) { m_name = name; }

	[[nodiscard]] QString getToken() const { return m_token; }
	void setToken(const QString& token) { m_token = token; }

private:
	UserManager() = default;

	int m_uid = 0;
	QString m_name;
	QString m_token;
};
