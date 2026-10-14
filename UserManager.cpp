#include "UserManager.h"

#include <QJsonArray>
#include <QJsonObject>

#include "TCPManager.h"

UserManager::~UserManager()
{
}

void UserManager::syncFriendRequestFromServer() const
{
    QJsonObject send_json;
    send_json["uid"] = QString::number(m_uid);
    TCPManager::GetInstance().sendJsonMessage(ReqId::ID_SYNC_FRIEND_REQUEST_REQ, send_json);
}

void UserManager::slotReceiveFriendApply(const std::uint64_t add_friend_request_id) const
{
    syncFriendRequestFromServer();
}

void UserManager::slotSyncFriendRequestFinish(std::vector<std::shared_ptr<ApplyInfo>> apply_infos)
{
    m_applyList = std::move(apply_infos);
}

void UserManager::slotSyncriendRequest() const
{
    syncFriendRequestFromServer();
}

UserManager::UserManager()
{
    // 连接收到好友请求
    connect(&TCPManager::GetInstance(), &TCPManager::sigReciveFriendApply, this, &UserManager::slotReceiveFriendApply);
    connect(&TCPManager::GetInstance(), &TCPManager::sigSyncFriendRequestFinish, this, &UserManager::slotSyncFriendRequestFinish);
    connect(this, &UserManager::sigSyncriendRequest, this, &UserManager::slotSyncriendRequest);

    // TODO: 从本地加载

}
