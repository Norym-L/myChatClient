#ifndef CHATCLIENT_MESSAGETEXTEDIT_H
#define CHATCLIENT_MESSAGETEXTEDIT_H

#include <QTextEdit>
#include "global.h"

class MessageTextEdit : public QTextEdit
{
    Q_OBJECT

public:
    explicit MessageTextEdit(QWidget* parent = nullptr);
    ~MessageTextEdit();

    QVector<MessageInfo> getMessageList();

protected:
    void keyPressEvent(QKeyEvent* e) override;        // 按下回车处理
    void dragEnterEvent(QDragEnterEvent* e) override; // 拖入文件处理
    void dropEvent(QDropEvent* e) override;           // 放下处理

    void insertFileFromUrl(const QStringList& urls);
    void insertImages(const QString& url);
    void insertTextFile(const QString& url);
    bool canInsertFromMimeData(const QMimeData* source) const override;
    void insertFromMimeData(const QMimeData* source) override;

private:
    static bool IsImage(const QString& url);
    static void InsertMessageList(QVector<MessageInfo>& list, MessageInfo::MessageFlag flag, const QString& text, const QPixmap& pix);
    static QStringList GetUrl(const QString& text);
    static QPixmap GetFileIconPixmap(const QString& url); // 获取文件图标信息，转化为图片
    static QString GetFileSize(quint64 size);             // 获取文件大小

signals:
    void sigSend();

private slots:
    void slotTextEditChaned();

private:
    QVector<MessageInfo> m_messageList;
    QVector<MessageInfo> m_getMessageList;
};


#endif //CHATCLIENT_MESSAGETEXTEDIT_H
