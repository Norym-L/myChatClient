import QtQuick
import ZmdUI 1.0

import "../components"

/*!
    新建群聊（1033 + 1035）。

    服务端的创建接口**不接受初始成员**，所以这个弹窗一次做完两件事：
    收集群名 + 勾选好友 → 页面先发 1033，拿到 group_id 后再发 1035 拉人。
    两步都在 GroupController 里串起来，这里只负责收集输入。

    展示组件约定：不读控制器 —— 好友列表 / busy / 提示都由页面写入，
    点「创建」只发 \l createRequested，成不成由页面回包后决定关不关窗。

    \note 服务端没有头像上传通道，也**没有群资料查询接口**，所以这里只让填群名：
          头像字段（`avator`）传空，群名会存进本地 `groups` 表 ——
          那是以后打开会话时唯一能拿到的名字。
*/

ZDialog {
    id: dialog

    width: 500
    title: "新建群聊"
    code: "GROUP-01"
    subtitle: "先建群，再拉人"
    showFooter: false

    // ── 由页面写入的数据 ─────────────────────────────────────────
    /// 好友列表（ContactController.friendList）
    property var friends: []
    /// 控制器是不是正在跑建群流程（按钮禁用，防手快连点建出两个群）
    property bool busy: false
    /// 控制器给的提示（失败原因 / 进度），显示在按钮上方
    property string hint: ""

    // ── 本地编辑态 ───────────────────────────────────────────────
    property string nameDraft: ""
    /// uid -> true 的选中集合（整体替换，见 FriendPicker 的说明）
    property var selected: ({})

    signal createRequested(string name, var memberUids)

    /// 打开前清干净：上一次填的群名 / 勾选不该带到下一次
    function openDialog() {
        nameDraft = ""
        selected = ({})
        hint = ""
        // ⚠ 单向写 text，不做双向绑定：绑定 + onTextChanged 回写会形成环，
        //   表现是输入一个字符光标就跳回开头（项目的输入框都按这个套路写）
        nameField.text = ""
        dialog.open()
    }

    function toggle(uid) {
        const next = Object.assign({}, selected)
        if (next[uid] === true)
            delete next[uid]
        else
            next[uid] = true
        selected = next
    }

    function selectedList() {
        const out = []
        for (const key in selected) {
            if (selected[key] === true)
                out.push(key)
        }
        return out
    }

    Column {
        width: parent.width
        spacing: Theme.spacingMd

        Text {
            width: parent.width
            text: "群名称"
            font: Theme.fontMonoSm
            color: Theme.muted
        }

        ZTextField {
            id: nameField
            width: parent.width
            placeholderText: "给这个群起个名字（必填）"
            onTextChanged: dialog.nameDraft = text
        }

        Text {
            width: parent.width
            text: "邀请好友（可不选，建完再拉）"
            font: Theme.fontMonoSm
            color: Theme.muted
        }

        FriendPicker {
            width: parent.width
            height: 240
            friends: dialog.friends
            selectedUids: dialog.selected
            emptyText: "还没有好友 —— 先到「联系人」里加一个"
            onToggled: (uid) => dialog.toggle(uid)
        }

        Text {
            width: parent.width
            visible: dialog.hint.length > 0
            text: dialog.hint
            wrapMode: Text.Wrap
            font: Theme.fontMonoSm
            color: Theme.muted
        }

        /*
            底部按钮：靠右排，左边次要、右边主要（和 ZDialog 自带底栏一致）。
            用「Item + anchors.right」而不是 Row.layoutDirection ——
            后者在本工程实测不镜像，按钮会左右颠倒。
        */
        Item {
            width: parent.width
            height: buttonRow.height

            Row {
                id: buttonRow
                anchors.right: parent.right
                spacing: Theme.spacingSm

                ZButton {
                    text: "取消"
                    variant: Theme.Variant.Secondary
                    enabled: !dialog.busy
                    onClicked: dialog.close()
                }

                ZButton {
                    text: dialog.busy ? "创建中…" : "创建群聊"
                    variant: Theme.Variant.Primary
                    enabled: !dialog.busy && dialog.nameDraft.trim().length > 0
                    onClicked: dialog.createRequested(dialog.nameDraft.trim(),
                                                      dialog.selectedList())
                }
            }
        }
    }
}
