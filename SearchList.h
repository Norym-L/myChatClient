#ifndef CHATCLIENT_SEARCHLIST_H
#define CHATCLIENT_SEARCHLIST_H

#include <QListWidget>
#include <QPointer>

#include "findresultdialog.h"
#include "UserData.h"
#include "loadingdialog.h"

class SearchList : public QListWidget
{
    Q_OBJECT

public:
    explicit SearchList(QWidget* parent = nullptr);

    void closeFindDialog();

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    void waitPending(bool pending = true);
    void addTipItem();

public slots:
    void slotSyncSearchEditChanged(const QString& text);

private slots:
    void slotItemClicked(QListWidgetItem* item);
    void slotUserSearchFinish(std::shared_ptr<SearchInfo> info); // 处理收到搜索结果

private:
    bool m_sendPending{false}; // 是否阻塞,等待服务器回包
    QPointer<FindResultDialog> m_findDialog{nullptr};
    LoadingDialog* m_loadingDialog{nullptr};
    QString m_searchEditText{};
};


#endif //CHATCLIENT_SEARCHLIST_H
