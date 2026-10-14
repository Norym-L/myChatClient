#pragma once
#include <QObject>
#include <QString>
#include <vector>

#include "singleton.h"
#include "UserData.h"

/**
 * @brief UserManager 保存当前登录用户的信息，来源为 /user_login 响应与
 *        ChatServer 登录响应（MESSAGE_CHAT_LOGIN_RESP）中的 uid / name / token
 */
class UserManager : public QObject, public Singleton<UserManager>
{
    friend class Singleton;

    Q_OBJECT

public:
    ~UserManager() override;

    [[nodiscard]] std::uint64_t getUid() const { return m_uid; }
    void setUid(const std::uint64_t uid) { m_uid = uid; }

    [[nodiscard]] QString getName() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    [[nodiscard]] QString getToken() const { return m_token; }
    void setToken(const QString& token) { m_token = token; }

    /// 获取用户申请列表
    [[nodiscard]] std::vector<std::shared_ptr<ApplyInfo>> getApplyList() const { return m_applyList; }

    /// 同步服务器的好友请求
    void syncFriendRequestFromServer() const;

signals:
    void sigSyncriendRequest();

public slots:
    void slotReceiveFriendApply(const std::uint64_t add_friend_request_id) const;
    void slotSyncFriendRequestFinish(std::vector<std::shared_ptr<ApplyInfo>> apply_infos);

    void slotSyncriendRequest() const;

private:
    UserManager();

    std::uint64_t m_uid = 0;
    QString m_name;
    QString m_token;
    std::vector<std::shared_ptr<ApplyInfo>> m_applyList;
};
