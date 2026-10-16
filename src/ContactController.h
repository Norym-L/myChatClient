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
 *   ContactController.sendFriendRequest(toUid, verifyInfo, remark, peerName)
 *   ContactController.refreshApplyList()
 *   读 searchResults / searchResultCount / applyList
 *     （searchResults 每项为 QVariantMap：uid / name / desc / sex / icon）
 *     （applyList 每项为 QVariantMap：id / fromUid / toUid / peerUid / received /
 *       name / verifyInfo / sex / state / stateText）
 *
 *   ⚠ applyList 里**我发出的和我收到的混在一起**（服务端查询是
 *     `applicant_uid = ? OR receiver_uid = ?`），而 name/icon/sex 取的是对方的。
 *     UI 要显示"对面是谁"，请一律用 **peerUid**，别用 fromUid；
 *     received 表示方向（true = 别人发给我的）。
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

    /**
     * 发送好友申请。
     *
     * @param toUid     目标 uid（字符串形式的 uint64）
     * @param verifyInfo 验证信息
     * @param remark    备注：加好友时给对方起的别名（服务端会落库）
     * @param peerName  对方的显示名。**只用于本地那条乐观记录** ——
     *                  AddFriendRsp 只回一个 request id，不带名字；
     *                  不传的话刚发出的那条申请只能显示 uid，直到下次刷新。
     */
    Q_INVOKABLE void sendFriendRequest(const QString& toUid, const QString& verifyInfo,
                                       const QString& remark, const QString& peerName);

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

    /**
     * 发送成功后，**立刻在本地补一条「已发出」的申请记录**。
     *
     * 为什么不等刷新：用户刚点完「加好友」，界面上必须马上能看到这条，
     * 更要紧的是 —— 弹窗要据此立刻切到「申请详情」，不再给「加好友」按钮，
     * 否则同一秒内连点就会发出多条重复申请。
     * 补出来的记录与服务端回包同形，下次 refreshApplyList() 会被真实数据覆盖。
     */
    void appendLocalSentRequest(std::uint64_t requestId);

    void onSearchResult(bool found, const QList<mychat::protocol::UserInfo>& infos, int error);
    void onFriendRequestResult(bool ok, int error, std::uint64_t requestId);
    void onFriendRequestList(const QList<mychat::protocol::FriendRequest>& requests, int error);

    QString m_tip;
    bool m_tip_is_error = false;
    bool m_busy = false;

    QVariantList m_search_results;
    QVariantList m_apply_list;

    // 本次「发送好友申请」的目标信息，成功后用来补那条本地记录
    std::uint64_t m_pending_peer_uid = 0;
    QString m_pending_peer_name;
    QString m_pending_verify;
};
