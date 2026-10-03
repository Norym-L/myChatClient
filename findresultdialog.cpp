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

    setWindowTitle("添加好友");
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);

    QString app_path = QCoreApplication::applicationDirPath();
    QString pix_path = QDir::toNativeSeparators(app_path + QDir::separator() + "static" + QDir::separator() + "head_1.jpg");

    QPixmap head_pix(pix_path);
    head_pix = head_pix.scaled(ui->head_label->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    ui->head_label->setPixmap(head_pix);
    ui->add_friend_button->setState("normal", "hover", "press");
    this->setModal(true);

    connect(ui->add_friend_button, &ClickedButton::released, this, &FindResultDialog::slotOnAddFriendButtonClicked);
}

FindResultDialog::~FindResultDialog()
{
    delete ui;
}

void FindResultDialog::setSearchInfo(const std::shared_ptr<SearchInfo>& si)
{
    m_searchInfo = si;
    ui->name_label->setText(m_searchInfo->userName);
}

void FindResultDialog::slotOnAddFriendButtonClicked()
{
    const auto apply_dialog = new ApplyFriend(m_parent);
    apply_dialog->show();
    hide();
}
