#include "logindialog.h"
#include "ui_logindialog.h"

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::LoginDialog)
{
    ui->setupUi(this);

    connect(ui->register_button, &QPushButton::clicked, this, &LoginDialog::switchRegister);

    ui->forget_label->setCursor(Qt::PointingHandCursor);
    ui->forget_label->setState("normal", "hover", "hover", "normal", "hover", "hover");
    connect(ui->forget_label, &ClickedLabel::clicked, this, [this]()
        {
            emit switchReset();
        });
}

LoginDialog::~LoginDialog()
{
    delete ui;
}