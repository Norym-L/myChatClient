#ifndef LOGINDIALOG_H
#define LOGINDIALOG_H

#include <QDialog>
#include <QMap>

#include "global.h"

namespace Ui {
class LoginDialog;
}

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog();
    bool checkAccountImputValid();
    bool checkPasswordImputValid();
    void addTipErr(TipErr tip, const QString& notice);
    void delTipErr(TipErr tip);
    void showTip(const QString& str, bool b_ok) const;

private:
    /// 登录处理
    void login();
    void initHttpHandlers();


    QMap<ReqId, std::function<void(const QJsonObject&)>> m_handlers;
    Ui::LoginDialog *ui;
    QMap<TipErr, QString> m_tipErrs;

private slots:
    void slot_login_mod_finish(ReqId id, QString res, ErrorCodes err);

signals:
    void switchRegister();
    void switchReset();
};

#endif // LOGINDIALOG_H
