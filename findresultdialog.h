#ifndef CHATCLIENT_FINDRESULTDIALOG_H
#define CHATCLIENT_FINDRESULTDIALOG_H

#include <QDialog>


QT_BEGIN_NAMESPACE

namespace Ui
{
class FindResultDialog;
}

QT_END_NAMESPACE

class FindResultDialog : public QDialog
{
    Q_OBJECT

public:
    explicit FindResultDialog(QWidget* parent = nullptr);
    ~FindResultDialog() override;

private:
    Ui::FindResultDialog* ui;
};


#endif //CHATCLIENT_FINDRESULTDIALOG_H
