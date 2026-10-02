#ifndef CHATCLIENT_SEARCHLIST_H
#define CHATCLIENT_SEARCHLIST_H

#include <QListWidget>

#include "UserData.h"
#include "loadingdialog.h"

class SearchList : public QListWidget
{
    Q_OBJECT

public:
    explicit SearchList(QWidget* parent = nullptr);

    void closeFindDialog();
    void setSearchedit(QWidget* widget);

protected:
    bool eventFilter(QObject* object, QEvent* event) override;

private:
    void waitPending(bool pending = true);
    void addTipItem();

private slots:
    void slotItemClicked(QListWidgetItem* item);
    void slotUserSearch(std::shared_ptr<SearchInfo> info);

private:
    bool m_sendPending{false}; // 是否阻塞,等待服务器回包
    std::shared_ptr<QDialog> m_findDialog{nullptr};
    QWidget* m_searchEdit{nullptr};
};


#endif //CHATCLIENT_SEARCHLIST_H
