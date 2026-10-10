#include "findresultdialog.h"
#include "ui_findresultdialog.h"

#include <QDir>
#include <QCoreApplication>
#include <QString>

#include "ApplyFriend.h"


FindResultDialog::FindResultDialog(QWidget* parent)
    : QDialog(parent), ui(new Ui::FindResultDialog), m_parent(parent)
{
    ui->setupUi(this);

    flashSearchInfo();
}

FindResultDialog::~FindResultDialog()
{
    delete ui;
}

void FindResultDialog::setSearchInfo(std::shared_ptr<SearchInfo> si)
{
    m_searchInfo = std::move(si);
    flashSearchInfo();
}

void FindResultDialog::flashSearchInfo()
{
    if (!m_searchInfo) // 空指针，未找到用户
    {
        setWindowTitle("未找到用户");

        ui->head_label->hide();
        ui->add_friend_button->setText("返回");
        ui->name_label->setText("未找到用户");

        connect(ui->add_friend_button, &ClickedButton::released, this, &FindResultDialog::slotOnAddFriendButtonClickednotFined);
    }
    else
    {
        setWindowTitle("添加好友");

        // 头像
        QString app_path = QCoreApplication::applicationDirPath();
        QString pix_path = QDir::toNativeSeparators(app_path + QDir::separator() + "static" + QDir::separator() + m_searchInfo->icon);
        QPixmap head_pix(pix_path);
        head_pix = head_pix.scaled(ui->head_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        ui->head_label->setPixmap(head_pix);

        ui->name_label->setText(m_searchInfo->userName);

        connect(ui->add_friend_button, &ClickedButton::released, this, &FindResultDialog::slotOnAddFriendButtonClickedifFined);
    }

    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    ui->add_friend_button->setState("normal", "hover", "press");
    this->setModal(true);
}

void FindResultDialog::slotOnAddFriendButtonClickedifFined()
{
    const auto apply_dialog = new ApplyFriend(m_parent);
    apply_dialog->show();
    hide();
}

void FindResultDialog::slotOnAddFriendButtonClickednotFined()
{
    hide();
}
