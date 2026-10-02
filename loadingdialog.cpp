#include "loadingdialog.h"
#include "ui_loadingdialog.h"

#include "QMovie"

LoadingDialog::LoadingDialog(QWidget* parent)
    : QDialog(parent), ui(new Ui::LoadingDialog)
{
    ui->setupUi(this);
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | Qt::WindowSystemMenuHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(parent->size());

    auto* movie = new QMovie(":/assets/images/loading.gif");
    ui->loading_label->setMovie(movie);
    movie->start();
}

LoadingDialog::~LoadingDialog()
{
    delete ui;
}
