#include "MessageTextEdit.h"

#include <qevent.h>
#include <QMessageBox>
#include <QMimeData>
#include <QFileIconProvider>
#include <QPainter>

#include "core/logger.h"

MessageTextEdit::MessageTextEdit(QWidget* parent)
    : QTextEdit(parent)
{
    setMaximumHeight(60);
}

MessageTextEdit::~MessageTextEdit() = default;

QVector<MessageInfo> MessageTextEdit::getMessageList()
{
    m_getMessageList.clear();

    auto doc = document()->toPlainText();
    QString text;
    int index_url = 0;
    const int count = m_messageList.size();
    for (auto index = 0; index < doc.size(); ++index)
    {
        if (doc[index] == QChar::ObjectReplacementCharacter)
        {
            if (!text.isEmpty())
            {
                InsertMessageList(m_getMessageList, MessageInfo::MessageFlag::TEXT, text, {});
                text.clear();
            }
            while (index_url < count)
            {
                MessageInfo msg = m_messageList[index_url];
                if (document()->toHtml().contains(msg.content, Qt::CaseSensitive))
                {
                    ++index_url;
                    m_getMessageList.append(msg);
                    break;
                }
                ++index_url;
            }
        }
        else
        {
            text.append(doc[index]);
        }
    }
    if (!text.isEmpty())
    {
        InsertMessageList(m_getMessageList, MessageInfo::MessageFlag::TEXT, text, {});
        text.clear();
    }
    m_messageList.clear();
    clear();
    return m_getMessageList;
}

void MessageTextEdit::keyPressEvent(QKeyEvent* e)
{
    if ((e->key() == Qt::Key_Enter || e->key() == Qt::Key_Return) && !(e->modifiers() & Qt::ShiftModifier))
    {
        LOG_DEBUG() << "[chat page] send message now";
        emit sigSend();
        return;
    }
    QTextEdit::keyPressEvent(e);
}

void MessageTextEdit::dragEnterEvent(QDragEnterEvent* e)
{
    if (e->source() == this)
    {
        e->ignore();
    }
    else
    {
        e->accept();
    }
}

void MessageTextEdit::dropEvent(QDropEvent* e)
{
    insertFromMimeData(e->mimeData());
    e->accept();
}

void MessageTextEdit::insertFileFromUrl(const QStringList& urls)
{
    if (urls.isEmpty())
    {
        return;
    }
    foreach(QString url, urls)
    {
        if (IsImage(url))
        {
            insertImages(url);
        }
        else
        {
            insertTextFile(url);
        }
    }
}

void MessageTextEdit::insertImages(const QString& url)
{
    LOG_DEBUG() << "[chat page] insert a image, url: " << url;
    QImage image(url);
    if (image.width() > PICTURE_MAX_WIDTH)
    {
        image = image.scaledToWidth(PICTURE_MAX_WIDTH, Qt::SmoothTransformation);
    }
    if (image.height() > PICTURE_MAX_HEIGHT)
    {
        image = image.scaledToWidth(PICTURE_MAX_HEIGHT, Qt::SmoothTransformation);
    }
    textCursor().insertImage(image, url);
    InsertMessageList(m_messageList, MessageInfo::MessageFlag::IMAGE, url, QPixmap::fromImage(image));
}

void MessageTextEdit::insertTextFile(const QString& url)
{
    LOG_DEBUG() << "[chat page] insert a file, url: " << url;
    const QFileInfo file_info(url);
    if (file_info.isDir())
    {
        QMessageBox::information(this, "提示", "不能上传文件夹！");
        return;
    }
    constexpr quint32 MAX_SIZE_OF_FILE = 100 * 1024 * 1024;
    if (file_info.size() > MAX_SIZE_OF_FILE)
    {
        QMessageBox::information(this, "提示", "发送的文件大小不能超过100M！");
        return;
    }
    const auto pix = GetFileIconPixmap(url);
    textCursor().insertImage(pix.toImage(), url);
    InsertMessageList(m_messageList, MessageInfo::MessageFlag::FILE, url, pix);
}

bool MessageTextEdit::canInsertFromMimeData(const QMimeData* source) const
{
    return QTextEdit::canInsertFromMimeData(source);
}

void MessageTextEdit::insertFromMimeData(const QMimeData* source)
{
    QStringList urls = GetUrl(source->text());
    if (urls.isEmpty())
    {
        return;
    }

    foreach(QString url, urls)
    {
        if (IsImage(url))
        {
            insertImages(url);
        }
        else
        {
            insertTextFile(url);
        }
    }
}

bool MessageTextEdit::IsImage(const QString& url)
{
    static const QString image_formats = "bmp,jpg,png,tif,gif,pcx,tga,exif,svg,psd,cdr,ufo,ai,eps,rwa,wmf";
    static QStringList image_formats_list = image_formats.split(",");

    const QFileInfo file_info(url);
    if (image_formats_list.contains(file_info.suffix(), Qt::CaseInsensitive))
    {
        return true;
    }
    else
    {
        return false;
    }
}

void MessageTextEdit::InsertMessageList(QVector<MessageInfo>& list, const MessageInfo::MessageFlag flag, const QString& text, const QPixmap& pix)
{
    list.append({
        .messageFlag = flag,
        .content     = text,
        .pixmap      = pix
    });
}

QStringList MessageTextEdit::GetUrl(const QString& text)
{
    QStringList urls{};
    if (text.isEmpty()) return urls;

    QStringList list = text.split("\n");
    foreach(QString url, list)
    {
        if (!url.isEmpty())
        {
            QStringList str = url.split("///");
            if (str.size() >= 2)
            {
                urls.append(str.at(2));
            }
        }
    }
    return urls;
}

QPixmap MessageTextEdit::GetFileIconPixmap(const QString& url)
{
    const QFileIconProvider provider;
    const QFileInfo file_info(url);
    const QIcon icon = provider.icon(file_info);

    const auto str_file_size = GetFileSize(file_info.size());
    const QFont font(QString("宋体"), 10, QFont::Normal, false);
    const QFontMetrics font_metrics(font);
    const QSize text_size = font_metrics.size(Qt::TextSingleLine, file_info.fileName());

    const QSize file_size = font_metrics.size(Qt::TextSingleLine, str_file_size);
    const auto max_width = text_size.width() > file_size.width() ? text_size.width() : file_size.width();
    QPixmap pix(50 + max_width + 10, 50);
    pix.fill();

    QPainter painter;
    painter.begin(&pix);

    // 文件图标
    const QRect rect(0, 0, 50, 50);
    painter.drawPixmap(rect, icon.pixmap(40, 40));
    painter.setPen(Qt::black);
    // 文件名称
    const QRect rect_text(50 + 10, 3, text_size.width(), text_size.height());
    painter.drawText(rect_text, file_info.fileName());
    // 文件大小
    const QRect rect_file(50 + 10, text_size.height() + 5, file_size.width(), file_size.height());
    painter.drawText(rect_file, str_file_size);

    painter.end();
    return pix;
}

QString MessageTextEdit::GetFileSize(const quint64 size)
{
    QString unit;
    double num;
    if (size < 1024)
    {
        num = size;
        unit = "B";
    }
    else if (size < 1024 * 1024)
    {
        num = size / 1024.0;
        unit = "KB";
    }
    else if (size < 1024 * 1024 * 1024)
    {
        num = size / 1024.0 / 1024.0;
        unit = "MB";
    }
    else
    {
        num = size / 1024.0 / 1024.0 / 1024.0;
        unit = "GB";
    }
    return QString::number(num, 'f', 2) + " " + unit;
}

void MessageTextEdit::slotTextEditChaned() {}
