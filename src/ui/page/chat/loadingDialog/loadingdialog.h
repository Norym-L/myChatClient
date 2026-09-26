#ifndef CHATCLIENT_LOADINGDIALOG_H
#define CHATCLIENT_LOADINGDIALOG_H

#include <QDialog>


QT_BEGIN_NAMESPACE

namespace Ui
{
class LoadingDialog;
}

QT_END_NAMESPACE

class LoadingDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoadingDialog(QWidget* parent = nullptr);
    ~LoadingDialog() override;

private:
    Ui::LoadingDialog* ui;
};


#endif //CHATCLIENT_LOADINGDIALOG_H
