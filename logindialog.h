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

    /// 设置登录按钮是否启用
    void enableLoginButton(bool enable) const;

    QMap<ReqId, std::function<void(const QJsonObject&)>> m_handlers;
    Ui::LoginDialog *ui;
    QMap<TipErr, QString> m_tipErrs;
    int m_uid;
    QString m_token;

private slots:
    void slot_login_mod_finish(ReqId id, QString res, ErrorCodes err);
    void slot_tcpConnectFinish(bool success);
    void slot_loginFailed(int error_code);

signals:
    void switchRegister();
    void switchReset();
    void sig_connectTcp(ServerInfo);
};

#endif // LOGINDIALOG_H
