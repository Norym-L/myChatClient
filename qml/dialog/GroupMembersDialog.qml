import QtQuick
import QtQuick.Controls.Basic
import ZmdUI 1.0

import "../components"

/*!
    群成员 / 群资料面板（1041 拉名单，1043 读资料，1035 拉人，1037 踢人，
    1039 解散，1045 改资料）。

    三个模式：
      · \b list —— 成员列表（群昵称 > 好友备注 > uid，角色、入群时间）+ 群资料；
      · \b add  —— 拉人：好友多选，确认后发 1035；
      · \b edit —— 改群名 / 群公告（群主或管理员，发 1045）。

    展示组件约定：不读控制器，数据与 busy / hint 全由页面写入，
    动作只发信号（\l addMembersRequested / \l kickRequested / \l dismissRequested /
    \l saveInfoRequested）。

    \note 服务端 7338a5c 起支持群资料查询（1043/1044）和修改（1045/1046），
          但**没有资料变更推送** —— 改完不会通知其他成员，本机保存成功后
          由页面再刷一次 1043（GroupController 负责）。
          头像仍不开放编辑：服务端没有上传通道。
*/

ZDialog {
    id: dialog

    width: 560
    title: "群成员"
    code: "GROUP-02"
    subtitle: "成员信息来自服务端"
    showFooter: false
    /// 关闭时把模式复位：下次打开不该停在上一次的"拉人"页
    onClosed: {
        dialog.mode = "list"
        dialog.confirmingDismiss = false
        dialog.selected = ({})
    }

    // ── 由页面写入 ───────────────────────────────────────────────
    property string gid: ""
    property string groupName: ""
    /// GroupController.memberList
    property var members: []
    /// GroupController.groupInfo（1044 回包转换后的 map；空对象 = 还没加载）
    property var groupInfo: ({})
    property bool infoLoading: false
    /// 2 群主 / 1 管理员 / 0 成员 / -1 未知
    property int myRole: -1
    property bool canManage: false
    property bool isOwner: false
    property bool busy: false
    property string hint: ""
    /// 拉人模式下可选的好友（ContactController.friendList）
    property var friends: []

    // ── 本地状态 ─────────────────────────────────────────────────
    /// "list" 成员列表 / "add" 拉人 / "edit" 改群资料
    property string mode: "list"
    /// 解散的二次确认（解散不可逆，不能点一下就发出去）
    property bool confirmingDismiss: false
    property var selected: ({})

    /// 编辑模式的草稿（单向写输入框，见 CreateGroupDialog 的说明）
    property string nameDraft: ""
    property string announcementDraft: ""

    signal addMembersRequested(var memberUids)
    signal kickRequested(string uid)
    signal dismissRequested()
    signal saveInfoRequested(string name, string announcement)

    /// 群资料里的群名优先；没有就用页面从本地会话缓存带进来的 groupName
    readonly property string profileName: {
        const loaded = dialog.groupInfo && dialog.groupInfo.loaded === true
        if (loaded && String(dialog.groupInfo.name).length > 0)
            return String(dialog.groupInfo.name)
        return dialog.groupName
    }

    readonly property string announcement: {
        if (dialog.groupInfo && dialog.groupInfo.loaded === true)
            return String(dialog.groupInfo.announcement)
        return ""
    }

    function openDialog() {
        mode = "list"
        confirmingDismiss = false
        selected = ({})
        prefillDrafts()
        dialog.open()
    }

    /// 把当前资料写进编辑草稿（服务端还没回资料时退回本地群名）
    function prefillDrafts() {
        nameDraft = profileName
        announcementDraft = announcement
        nameField.text = nameDraft
        announcementArea.text = announcementDraft
    }

    function enterEditMode() {
        prefillDrafts()
        mode = "edit"
        confirmingDismiss = false
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

    function roleLabel(role) {
        if (role === 2)
            return "群主"
        if (role === 1)
            return "管理员"
        return "成员"
    }

    Column {
        width: parent.width
        spacing: Theme.spacingMd

        // ── 群信息（名字 / 群号 / 人数 / 群主 / 创建时间 / 我的身份）──
        Column {
            width: parent.width
            spacing: 4

            Text {
                width: parent.width
                elide: Text.ElideRight
                text: dialog.profileName.length > 0
                      ? dialog.profileName
                      : ("群 " + dialog.gid)
                font: Theme.fontHeading
                color: Theme.ink
            }

            Text {
                width: parent.width
                elide: Text.ElideRight
                text: "群号 " + dialog.gid + "  ·  " + dialog.members.length + " 人"
                      + (dialog.myRole >= 0 ? ("  ·  我是" + dialog.roleLabel(dialog.myRole)) : "")
                font: Theme.fontMonoSm
                color: Theme.muted
            }

            Text {
                width: parent.width
                elide: Text.ElideRight
                visible: dialog.groupInfo && dialog.groupInfo.loaded === true
                text: {
                    if (!dialog.groupInfo || dialog.groupInfo.loaded !== true)
                        return ""
                    const parts = []
                    parts.push("群主 " + dialog.groupInfo.ownerUid)
                    if (Number(dialog.groupInfo.maxMembers) > 0)
                        parts.push("上限 " + dialog.groupInfo.maxMembers + " 人")
                    if (Number(dialog.groupInfo.createAt) > 0) {
                        const date = new Date(Number(dialog.groupInfo.createAt))
                        parts.push("创建于 " + date.getFullYear() + "-"
                                   + ("0" + (date.getMonth() + 1)).slice(-2) + "-"
                                   + ("0" + date.getDate()).slice(-2))
                    }
                    return parts.join("  ·  ")
                }
                font: Theme.fontMonoSm
                color: Theme.muted
            }

            Text {
                width: parent.width
                visible: dialog.infoLoading && !(dialog.groupInfo && dialog.groupInfo.loaded === true)
                text: "正在获取群资料…"
                font: Theme.fontMonoSm
                color: Theme.muted
            }
        }

        // ── 群公告（只读展示；编辑在 edit 模式）──
        Column {
            width: parent.width
            spacing: 4
            visible: dialog.mode === "list" && dialog.announcement.length > 0

            Text {
                text: "群公告"
                font: Theme.fontMonoSm
                color: Theme.muted
            }

            Text {
                width: parent.width
                text: dialog.announcement
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                font: Theme.fontBody
                color: Theme.ink
            }
        }

        Rectangle {
            width: parent.width
            height: 1
            color: Theme.lineSoft
            visible: dialog.mode !== "edit"
        }

        // ══════════ 成员列表 ══════════
        Item {
            width: parent.width
            height: Math.min(320, Math.max(80, dialog.members.length * 46))
            visible: dialog.mode === "list"

            Flickable {
                id: memberFlick
                anchors.fill: parent
                clip: true
                contentHeight: memberColumn.height
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: memberColumn
                    width: memberFlick.width
                    spacing: 0

                    Repeater {
                        model: dialog.members

                        Item {
                            id: memberRow
                            required property var modelData
                            width: memberColumn.width
                            height: 46

                            readonly property string rowUid: String(modelData.uid)
                            readonly property string rowName: {
                                const display = modelData.displayName
                                if (display !== undefined && String(display).length > 0)
                                    return String(display)
                                return rowUid
                            }
                            readonly property bool rowOwner: modelData.isOwner === true
                            readonly property bool rowMe: modelData.isMe === true

                            Rectangle {
                                anchors.fill: parent
                                color: "transparent"
                            }

                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: Theme.lineSoft
                                opacity: 0.5
                            }

                            ZAvatar {
                                id: memberAvatar
                                anchors.left: parent.left
                                anchors.verticalCenter: parent.verticalCenter
                                size: 28
                                name: memberRow.rowName
                                uid: memberRow.rowUid
                            }

                            Text {
                                anchors.left: memberAvatar.right
                                anchors.leftMargin: Theme.spacingSm
                                anchors.right: memberRole.left
                                anchors.rightMargin: Theme.spacingSm
                                anchors.verticalCenter: parent.verticalCenter
                                elide: Text.ElideRight
                                text: memberRow.rowName
                                      + (memberRow.rowMe ? "（我）" : "")
                                      + "  ·  " + memberRow.rowUid
                                font: Theme.fontBody
                                color: Theme.ink
                            }

                            Text {
                                id: memberRole
                                anchors.right: parent.right
                                anchors.rightMargin: memberRow.modelData.canKick === true
                                    ? 44 : Theme.spacingSm
                                anchors.verticalCenter: parent.verticalCenter
                                text: dialog.roleLabel(Number(memberRow.modelData.role))
                                font: Theme.fontMonoSm
                                color: memberRow.rowOwner ? Theme.accent : Theme.muted
                            }

                            // 踢人：能不能点由服务端规则算好（modelData.canKick）
                            ZIconButton {
                                anchors.right: parent.right
                                anchors.rightMargin: 4
                                anchors.verticalCenter: parent.verticalCenter
                                visible: memberRow.modelData.canKick === true
                                width: 28
                                height: 28
                                implicitWidth: 28
                                implicitHeight: 28
                                glyphSize: 13
                                glyph: "×"
                                tooltip: "移出群聊"
                                enabled: !dialog.busy
                                onClicked: dialog.kickRequested(memberRow.rowUid)
                            }
                        }
                    }

                    Text {
                        width: memberColumn.width
                        height: 70
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        visible: dialog.members.length === 0
                        text: dialog.busy ? "正在获取成员…" : "还没有成员信息"
                        font: Theme.fontBody
                        color: Theme.muted
                    }
                }
            }
        }

        // ══════════ 拉人 ══════════
        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: dialog.mode === "add"

            FriendPicker {
                width: parent.width
                height: 260
                friends: dialog.friends
                selectedUids: dialog.selected
                emptyText: "没有可拉的好友"
                onToggled: (uid) => dialog.toggle(uid)
            }
        }

        // ══════════ 改群资料 ══════════
        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: dialog.mode === "edit"

            Text {
                text: "群名称"
                font: Theme.fontMonoSm
                color: Theme.muted
            }

            ZTextField {
                id: nameField
                width: parent.width
                placeholderText: "群名称（必填，最多 32 个字）"
                maximumLength: 32
                onTextChanged: dialog.nameDraft = text
            }

            Text {
                text: "群公告（留空 = 清除公告）"
                font: Theme.fontMonoSm
                color: Theme.muted
            }

            /*
                公告可能带换行，ZTextField 是单行的装不下，这里用原生 TextArea
                套一个和 ZTextField 同色系的边框；保持"展示组件不读控制器"的约定。
            */
            Rectangle {
                width: parent.width
                height: 96
                color: Theme.surface
                border.color: announcementArea.activeFocus ? Theme.accent : Theme.line
                border.width: Theme.border

                TextArea {
                    id: announcementArea
                    anchors.fill: parent
                    anchors.margins: Theme.spacingSm
                    padding: 0
                    color: Theme.ink
                    placeholderText: "写点什么给群成员看（可留空）"
                    placeholderTextColor: Theme.muted
                    font: Theme.fontBody
                    wrapMode: TextArea.Wrap
                    selectByMouse: true
                    background: null
                    onTextChanged: dialog.announcementDraft = text
                }
            }

            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: "提示：群资料修改成功后，其他成员不会立刻看到 —— 服务端没有资料变更推送。"
                font: Theme.fontMonoSm
                color: Theme.muted
            }
        }

        // ── 提示（失败原因 / 进度）──
        Text {
            width: parent.width
            visible: dialog.hint.length > 0
            text: dialog.hint
            wrapMode: Text.Wrap
            font: Theme.fontMonoSm
            color: Theme.muted
        }

        // ── 解散的二次确认 ──
        Column {
            width: parent.width
            spacing: Theme.spacingSm
            visible: dialog.confirmingDismiss

            Text {
                width: parent.width
                wrapMode: Text.Wrap
                text: "解散后这个群在服务端立即删除，成员不会收到任何通知；"
                      + "本机的聊天记录保留。确定解散？"
                font: Theme.fontBody
                color: Theme.danger
            }
        }

        // ── 底部按钮（靠右；左次要、右主要）──
        Item {
            width: parent.width
            height: buttonRow.height

            Row {
                id: buttonRow
                anchors.right: parent.right
                spacing: Theme.spacingSm

                // 列表模式：关闭 / 解散 / 拉人
                ZButton {
                    visible: dialog.mode === "list" && !dialog.confirmingDismiss
                    text: dialog.busy ? "处理中…" : "关闭"
                    variant: Theme.Variant.Secondary
                    onClicked: dialog.close()
                }

                ZButton {
                    visible: dialog.mode === "list" && !dialog.confirmingDismiss
                           && dialog.isOwner
                    text: "解散群聊"
                    variant: Theme.Variant.Secondary
                    enabled: !dialog.busy
                    onClicked: dialog.confirmingDismiss = true
                }

                ZButton {
                    visible: dialog.mode === "list" && !dialog.confirmingDismiss
                           && dialog.canManage
                    text: "编辑资料"
                    variant: Theme.Variant.Secondary
                    enabled: !dialog.busy
                    onClicked: dialog.enterEditMode()
                }

                ZButton {
                    visible: dialog.mode === "list" && !dialog.confirmingDismiss
                           && dialog.canManage
                    text: "拉人入群"
                    variant: Theme.Variant.Primary
                    enabled: !dialog.busy
                    onClicked: {
                        dialog.selected = ({})
                        dialog.mode = "add"
                    }
                }

                // 解散确认态
                ZButton {
                    visible: dialog.confirmingDismiss
                    text: "再想想"
                    variant: Theme.Variant.Secondary
                    onClicked: dialog.confirmingDismiss = false
                }

                ZButton {
                    visible: dialog.confirmingDismiss
                    text: "确认解散"
                    variant: Theme.Variant.Primary
                    enabled: !dialog.busy
                    onClicked: dialog.dismissRequested()
                }

                // 拉人模式
                ZButton {
                    visible: dialog.mode === "add"
                    text: "返回"
                    variant: Theme.Variant.Secondary
                    enabled: !dialog.busy
                    onClicked: {
                        dialog.mode = "list"
                        dialog.selected = ({})
                    }
                }

                ZButton {
                    visible: dialog.mode === "add"
                    text: dialog.busy ? "提交中…" : ("拉 " + dialog.selectedList().length + " 人入群")
                    variant: Theme.Variant.Primary
                    enabled: !dialog.busy && dialog.selectedList().length > 0
                    onClicked: dialog.addMembersRequested(dialog.selectedList())
                }

                // 编辑资料模式
                ZButton {
                    visible: dialog.mode === "edit"
                    text: "返回"
                    variant: Theme.Variant.Secondary
                    enabled: !dialog.busy
                    onClicked: dialog.mode = "list"
                }

                ZButton {
                    visible: dialog.mode === "edit"
                    text: dialog.busy ? "保存中…" : "保存资料"
                    variant: Theme.Variant.Primary
                    enabled: !dialog.busy && dialog.nameDraft.trim().length > 0
                    onClicked: dialog.saveInfoRequested(dialog.nameDraft.trim(),
                                                        dialog.announcementDraft)
                }
            }
        }
    }
}
