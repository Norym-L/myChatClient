#include "ApplyFriendPage.h"

#include "ApplyFriendList.h"
#include "TCPManager.h"
#include "UserManager.h"

#ifdef _DEBUG
#include <QRandomGenerator>
#endif


ApplyFriendPage::ApplyFriendPage(QWidget *parent)
    : QWidget(parent), ui(new Ui::ApplyFriendPageClass())
{
    ui->setupUi(this);

    loadApplyList();
    
    connect(ui->friend_apply_list, &ApplyFriendList::sigShowSearch, this, &ApplyFriendPage::sigShowSearch);
    connect(&TCPManager::GetInstance(), &TCPManager::sigAuthRsp, this, &ApplyFriendPage::slotAuthRsp);
}

ApplyFriendPage::~ApplyFriendPage()
{
    delete ui;
}

void ApplyFriendPage::addNewApply(std::shared_ptr<ApplyInfo> apply)
{
    auto* apply_item = new ApplyFriendItem();
    apply_item->setInfo(std::move(apply));
    auto* item = new QListWidgetItem();
    item->setSizeHint(apply_item->sizeHint());
    item->setFlags(item->flags() & ~Qt::ItemIsEnabled & ~Qt::ItemIsSelectable);
    ui->friend_apply_list->insertItem(0, item);
    ui->friend_apply_list->setItemWidget(item, apply_item);

    if (apply_item->getInfo()->status == Status::WAITING)
    {
        apply_item->showAddButton(true); // 显示添加按钮
        // TODO 处理好友申请
        connect(apply_item, &ApplyFriendItem::sigAuthFriend, [this](std::shared_ptr<ApplyInfo> apply_info)
            {
                // auto* auth_friend = new AuthenFriend(this);
                // auth_friend->setModal(true);
                // auth_friend->setApplyInfo(apply_info);
                // auth_friend->show();
            });
    }
    else
    {
        apply_item->showAddButton(false);
        apply_item->setTipText(apply_item->getInfo()->status == Status::ACCEPTED ? tr("已接受") : tr("已拒绝"));
    }
}

void ApplyFriendPage::paintEvent(QPaintEvent* event)
{
    QStyleOption option;
    option.initFrom(this);
    QPainter p(this);
    style()->drawPrimitive(QStyle::PE_Widget, &option, &p, this);
}

void ApplyFriendPage::loadApplyList()
{
    // 添加好友申请
    for (const auto& apply : UserManager::GetInstance().getApplyList())
    {
        addNewApply(apply);
    }

#ifdef _DEBUG // 测试添加好友
    static std::vector<QString> strs = {
        "hello world !",
        "nice to meet u",
        "New year，new life",
        "You have to love yourself",
        "My love is written in the wind ever since the whole world is you"
    };
    static std::vector<QString> heads = {
        ":/assets/images/head_1.jpg",
        ":/assets/images/head_2.jpg",
        ":/assets/images/head_3.jpg",
        ":/assets/images/head_4.jpg",
        ":/assets/images/head_5.jpg"
    };
    static std::vector<QString> names = {
        "llfc",
        "zack",
        "golang",
        "cpp",
        "java",
        "nodejs",
        "python",
        "rust"
    };

    for (int i = 0; i < 13; ++i)
    {
        const int random_value = QRandomGenerator::global()->bounded(100);
        const auto str_i = random_value % strs.size();
        const auto head_i = random_value % heads.size();
        const auto name_i = random_value % names.size();

        auto* contact_user_widget = new ApplyFriendItem();
        const auto info = std::make_shared<ApplyInfo>(0, names[name_i], "测试用说明", heads[head_i], "", Sex::UNKNOWN, Status::WAITING);
        addNewApply(info);
    }
#endif
}

void ApplyFriendPage::slotAuthRsp(std::shared_ptr<AuthRsp> auth_rsp)
{
    const auto uid = auth_rsp->uid;
    const auto it = m_unauthItems.find(uid);
    if (it == m_unauthItems.end())
    {
        return;
    }

    it->second->showAddButton(false);
    it->second->setTipText(auth_rsp->status == Status::ACCEPTED ? tr("已接受") : tr("已拒绝"));
}

