#ifndef CHATCLIENT_CHATUSERLIST_H
#define CHATCLIENT_CHATUSERLIST_H

#include <QListWidget>

class ChatUserList : public QListWidget
{
    Q_OBJECT
public:
    explicit ChatUserList(QWidget *parent = nullptr);

signals:
    // 滚动到底部时发出，通知外部加载更多联系人。
    // 注意：signal 必须有 moc 生成的实现，声明在 private 段且用 emit 调用
    // 会导致链接期 undefined reference。
    void sigLoadingChatUser();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;
};


#endif //CHATCLIENT_CHATUSERLIST_H
