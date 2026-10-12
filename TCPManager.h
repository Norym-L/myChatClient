#pragma once
#include <QObject>
#include <qtcpsocket.h>
#include <QString>
#include <QByteArray>
#include <QJsonObject>
#include <QTimer>

#include <functional>
#include <map>
#include <optional>

#include "singleton.h"
#include "global.h"
#include "UserData.h"

/**
 * @brief TCPManager 与 ChatServer 的裸 TCP 连接（详见 ServerDocs/api-tcp.md）
 *
 * - 帧格式：[message_id:2 大端][body_len:2 大端][body:body_len 字节 UTF-8 JSON]
 * - 登录流程：/user_login 拿到 host/port/token 后连接 ChatServer，
 *   发送 message_id = 0 的登录帧，等待 message_id = 1 的响应帧。
 */
class TCPManager : public QObject, public Singleton<TCPManager>
{
    friend class Singleton;

    Q_OBJECT

public:
    /**
     * @brief sendChatMessage 按帧协议发送一条消息
     * @param req_id 客户端请求编号，需能映射到线上 message_id
     * @param body 消息体，序列化为紧凑 JSON
     * @return false 表示未连接、编号无对应 message_id 或 body 超过 2048 字节
     */
    bool sendJsonMessage(ReqId req_id, const QJsonObject& body);

    /// 当前是否已与 ChatServer 建立连接
    [[nodiscard]] bool isConnected() const;

private:
    /// 连接状态
    enum class LinkState : std::uint8_t
    {
        Idle = 0,
        Connecting,
        Connected,
    };

    TCPManager();

    void initHandlers();

    /**
     * @brief dispatchBuffer 按帧头拆包，一次可能收到多帧或半帧
     */
    void dispatchBuffer();

    /**
     * @brief handleMessage 按线上 message_id 分发到处理函数
     */
    void handleMessage(std::uint16_t message_id, const QByteArray& body);
    /// 关闭并复位会话（不再对外发信号）
    void closeSession(const QString& reason);
    /// 转化为qjson
    static QJsonObject ParseToJson(const QByteArray& body);

    std::map<ReqId, std::function<void(const QByteArray&)>> m_handlers;
    QByteArray m_buffer;
    QTcpSocket m_socket;
    QTimer m_connect_timer;
    QString m_host;
    quint16 m_port;
    LinkState m_link_state;

public slots:
    /// 连接 /user_login 分配到的 ChatServer
    void slot_tcpConnect(ServerInfo server_info);
    void slotSendData(ReqId req_id, QByteArray data);

signals:
    /// 连接结果；false 表示连接失败或超时
    void sig_connectSuccess(bool be_success);
    void sig_loginFailed(int error);
    void sig_switchToChatDialog();
    /// 连接建立之后的链路异常（已断开 / 报文非法）
    void sig_tcpError(const QString& reason);

    void sigUserSearchFinish(std::shared_ptr<SearchInfo> info); // 用户搜索收到回包
    void sigSendData(ReqId id, QByteArray data); // 发送数据

    void sigReciveFriendApply(std::shared_ptr<AddFriendApply>); // 收到添加好友申请
    void sigAddAuthFriend(std::shared_ptr<AuthInfo>); // 对端同意添加好友
    void sigAuthRsp(std::shared_ptr<AuthRsp>); // 自己同意添加好友
};
