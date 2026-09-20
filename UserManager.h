#pragma once
#include <QObject>
#include <QString>

#include "singleton.h"

class UserManager :public QObject, public Singleton<UserManager>
{
	friend class Singleton;

	Q_OBJECT

public:
	~UserManager() override;

	[[nodiscard]] size_t getUid() const { return m_uid; }
	void setUid(const size_t uid) { m_uid = uid; }

	[[nodiscard]] QString getName() const { return m_name; }
	void setName(const QString& name) { m_name = name; }

	[[nodiscard]] QString getToken() const { return m_token; }
	void setToken(const QString& token) { m_token = token; }

private:
	UserManager()
		: m_uid(0)
	{}

	size_t m_uid;
	QString m_name;
	QString m_token;
};
