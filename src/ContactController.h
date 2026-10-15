#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <cstdint>

#include "protocol.h"

/**
 * ContactController —— 联系人 / 好友链路控制器（QML 与 core 之间的唯一入口）
 *
 * 覆盖服务端**已经实现**的三条 TCP 消息：
 *   搜索用户             SearchUserReq/Rsp
 *   发送好友申请         AddFriendReq/Rsp
 *   好友申请列表         SyncFriendRequestReq/Rsp
 *
 * ⚠ 服务端目前**没有实现**好友列表（`getUserApplyListAndFriendList` 是空函数），
 *   也没有文本聊天消息。所以「会话列表 / 消息气泡」现在拿不到任何真实数据，
 *   这部分留给服务端补齐后再接。
 *
 * QML 侧：
 *   ContactController.searchUser(keyword)
 *   ContactController.sendFriendRequest(toUid, verifyInfo, remark)
 *   ContactController.refreshApplyList()
 *   读 searchResults / searchResultCount / applyList
 *     （searchResults 每项为 QVariantMap：uid / name / desc / sex / icon）
 */
class ContactController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString tip READ tip NOTIFY tipChanged)
    Q_PROPERTY(bool tipIsError READ tipIsError NOTIFY tipIsErrorChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

    /// 最近一次搜索的结果列表（服务端可能命中多个同名/相似用户，可返回多条）
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY searchResultsChanged)
    /// 本次搜索命中条数（0 = 未找到）
    Q_PROPERTY(int searchResultCount READ searchResultCount NOTIFY searchResultsChanged)

    /// 好友申请列表，每项是一个 QVariantMap，供 Repeater / ListView 直接用
    Q_PROPERTY(QVariantList applyList READ applyList NOTIFY applyListChanged)

public:
    explicit ContactController(QObject* parent = nullptr);

    [[nodiscard]] QString tip() const { return m_tip; }
    [[nodiscard]] bool tipIsError() const { return m_tip_is_error; }
    [[nodiscard]] bool busy() const { return m_busy; }

    [[nodiscard]] QVariantList searchResults() const { return m_search_results; }
    [[nodiscard]] int searchResultCount() const { return static_cast<int>(m_search_results.size()); }

    [[nodiscard]] QVariantList applyList() const { return m_apply_list; }

    /// 搜索用户：keyword 可以是 uid，也可以是用户名（服务端两种都认）
    Q_INVOKABLE void searchUser(const QString& keyword);

    /// 发送好友申请
    Q_INVOKABLE void sendFriendRequest(const QString& toUid, const QString& verifyInfo,
                                       const QString& remark);

    /// 刷新「别人发给我的」好友申请列表
    Q_INVOKABLE void refreshApplyList();

    /// 清空当前搜索结果（比如用户改了关键字）
    Q_INVOKABLE void clearSearchResult();

signals:
    void tipChanged();
    void tipIsErrorChanged();
    void busyChanged();
    void searchResultsChanged();
    void applyListChanged();

    /// 搜索结束；found=false 时 error 是服务端错误码（未找到通常是 1014）
    void searchFinished(bool found, int error);
    /// 好友申请发送结果
    void friendRequestSent(bool ok, int error);
    /// 本地校验失败，field 为 keyword / toUid
    void validationFailed(const QString& field);

private:
    void setTip(const QString& text, bool is_error = false);
    void setBusy(bool busy);

    /// 自己的 uid；未登录时为 0
    [[nodiscard]] std::uint64_t selfUid() const;

    void onSearchResult(bool found, const QList<mychat::protocol::UserInfo>& infos, int error);
    void onFriendRequestResult(bool ok, int error, std::uint64_t requestId);
    void onFriendRequestList(const QList<mychat::protocol::FriendRequest>& requests, int error);

    QString m_tip;
    bool m_tip_is_error = false;
    bool m_busy = false;

    QVariantList m_search_results;
    QVariantList m_apply_list;
};
