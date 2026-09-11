#include "registerdialog.h"
#include "ui_registerdialog.h"

#include "global.h"
#include "httpmanager.h"

RegisterDialog::RegisterDialog(QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::RegisterDialog)
{
    ui->setupUi(this);
    ui->pass_edit->setEchoMode(QLineEdit::Password);
    ui->pass2_edit->setEchoMode(QLineEdit::Password);

    ui->err_tip->setProperty("state", "normal");
    repolish(ui->err_tip);

    connect(&HttpManager::GetInstance(), &HttpManager::sig_reg_http_finish, this, &RegisterDialog::slot_reg_mod_finish);

    // 注册回调函数
    initHttpHandlers();
}

RegisterDialog::~RegisterDialog()
{
    delete ui;
}

void RegisterDialog::on_get_token_button_clicked()
{
    auto email = ui->email_edit->text();
    QRegularExpression regex(R"((\w+)(\.|_)?(\w*)@(\w+)(\.(\w+))+)");
    bool match = regex.match(email).hasMatch();
    if (match)
    {
        // 发送验证码
        QJsonObject json_obj;
        json_obj["email"] = email;
        HttpManager::GetInstance().postHttpReq(QUrl(GATE_URL_PERFIX + "/get_Verifycode"),
                                                json_obj,
                                                ReqId::ID_GET_VERIFY_CODE,
                                                Modules::REGISTERMOD);
    }
    else
    {
        showTip(tr("邮箱地址不准确"), false);
    }
}

void RegisterDialog::slot_reg_mod_finish(ReqId id, QString res, ErrorCodes err)
{
    if (err != ErrorCodes::SUCCESS)
    {
        showTip(tr("网络请求错误"), false);
        return;
    }

    // json解析
    QJsonDocument jsonDoc = QJsonDocument::fromJson(res.toUtf8());
    if (jsonDoc.isNull())
    {
        showTip(tr("josn解析失败"), false);
        return;
    }
    if (!jsonDoc.isObject())
    {
        showTip(tr("json解析失败"), false);
        return;
    }

    m_handlers[id](jsonDoc.object());

    return;
}

void RegisterDialog::initHttpHandlers()
{
    // 获取验证码回调函数
    m_handlers.insert(ReqId::ID_GET_VERIFY_CODE, [this](const QJsonObject& jsonObj){
        int error = jsonObj["error"].toInt();
        if (error != ErrorCodes::SUCCESS)
        {
            showTip(tr("参数错误"), false);
            qDebug() << "error: " << error << "  json: " << jsonObj << "\n";
            return;
        }

        auto email = jsonObj["email"].toString();
        showTip(tr("验证码已发送到邮箱，注意查收"), true);
        qDebug() << "email is: " << email;
    });

    // 注册用户回包逻辑
    m_handlers.insert(ReqId::ID_REG_USER, [this](const QJsonObject& jsonObj){
        int error = jsonObj["error"].toInt();
        if (error != ErrorCodes::SUCCESS)
        {
            showTip(tr("参数错误"), false);
            return;
        }

        auto email = jsonObj["email"].toString();
        showTip(tr("注册成功"), true);
        qDebug() << "email is " << email;
    });
}

void RegisterDialog::showTip(const QString& str, bool b_ok)
{
    if (b_ok)
    {
        ui->err_tip->setProperty("state", "normal");
    }
    else
    {
        ui->err_tip->setProperty("state", "err");
    }
    ui->err_tip->setText(str);

    repolish(ui->err_tip);
}


void RegisterDialog::on_confirm_button_clicked()
{
    if(ui->user_name_edit->text() == ""){
        showTip(tr("用户名不能为空"), false);
        return;
    }

    if(ui->email_edit->text() == ""){
        showTip(tr("邮箱不能为空"), false);
        return;
    }

    if(ui->pass_edit->text() == ""){
        showTip(tr("密码不能为空"), false);
        return;
    }

    if(ui->pass2_edit->text() == ""){
        showTip(tr("确认密码不能为空"), false);
        return;
    }

    if(ui->pass2_edit->text() != ui->pass_edit->text()){
        showTip(tr("密码和确认密码不匹配"), false);
        return;
    }

    if(ui->token_edit->text() == ""){
        showTip(tr("验证码不能为空"), false);
        return;
    }

    //day11 发送http请求注册用户
    QJsonObject json_obj;
    json_obj["user"] = ui->user_name_edit->text();
    json_obj["email"] = ui->email_edit->text();
    json_obj["password"] = ui->pass_edit->text(); // 密码
    json_obj["confirm"] = ui->pass2_edit->text(); // 密码2确认密码
    json_obj["verify_code"] = ui->token_edit->text(); // 验证码
    HttpManager::GetInstance().postHttpReq(QUrl(GATE_URL_PERFIX+"/user_register"),
                                           json_obj,
                                           ReqId::ID_REG_USER,
                                           Modules::REGISTERMOD);
}

