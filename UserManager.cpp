#include "UserManager.h"

#include "TCPManager.h"

UserManager::~UserManager()
{
}

void UserManager::slotReceiveFriendApply(const std::uint64_t add_friend_request_id)
{
    m_friendRequestIds.push_back(add_friend_request_id);
}

UserManager::UserManager()
{
    // 连接收到好友请求
    connect(&TCPManager::GetInstance(), &TCPManager::sigReciveFriendApply, this, &UserManager::slotReceiveFriendApply);

    // TODO: 从本地加载

}
