#ifndef REGISTERDIALOG_H
#define REGISTERDIALOG_H

#include <QDialog>

namespace Ui {
class RegisterDialog;
}

class RegisterDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RegisterDialog(QWidget *parent = nullptr);
    ~RegisterDialog();

private slots:
    void on_get_token_button_clicked();

private:
    void showTip(const QString& str, bool b_ok);
    Ui::RegisterDialog *ui;
};

#endif // REGISTERDIALOG_H
