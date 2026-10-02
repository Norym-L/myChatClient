#include "findresultdialog.h"
#include "ui_findresultdialog.h"


FindResultDialog::FindResultDialog(QWidget* parent) : QDialog(parent), ui(new Ui::FindResultDialog)
{
    ui->setupUi(this);
}

FindResultDialog::~FindResultDialog()
{
    delete ui;
}
