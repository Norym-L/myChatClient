import QtQuick
import ZmdUI 1.0

import MyChat.Core 1.0
import "../../components"

/*!
    好友申请分区（左侧导航第 3 项）。

    数据全部来自 ContactController.applyList（服务端 1008 回包），本组件只负责
    把它们摆出来 —— 点整行只抛信号，资料卡由 ChatPage 打开。

    \note 列表项里的 `name` / `nick` / `sex` 取的都是**对方**的资料，
          而 `fromUid` 在「我发出的」那条里是**我本人** —— 所以头像 / 在线状态
          一律用 `peerUid`（踩过的坑，见 ContactController 的类注释）。
*/

Column {
    id: pane

    /// 请求打开某条申请人的资料卡（带申请详情，页面负责弹窗）
    signal userInfoRequested(var user, var request)

    anchors.fill: parent
    anchors.margins: Theme.spacingLg
    spacing: Theme.spacingMd

    Row {
        width: parent.width
        spacing: Theme.spacingMd

        ZSectionTitle {
            width: parent.width - refreshBtn.width - parent.spacing
            index: "02"
            eyebrow: "FRIEND REQUESTS"
            title: "好友申请"
            subtitle: "共 " + ContactController.applyList.length + " 条"
        }

        ZButton {
            id: refreshBtn
            anchors.bottom: parent.bottom
            text: ContactController.busy ? "…" : "刷新"
            variant: Theme.Variant.Secondary
            enabled: !ContactController.busy
            onClicked: ContactController.refreshApplyList()
        }
    }

    Text {
        width: parent.width
        visible: ContactController.applyList.length === 0
        text: "暂无好友申请"
        font: Theme.fontBody
        color: Theme.muted
    }

    // 申请列表：ZListView 的 delegate 用自定义项，
    // 因为 ZListItem 只有 label/description/trailing，放不下头像
    Flickable {
        width: parent.width
        height: parent.height - 100
        contentHeight: applyCol.implicitHeight
        clip: true

        Column {
            id: applyCol
            width: parent.width
            spacing: Theme.spacingSm

            Repeater {
                model: ContactController.applyList

                Rectangle {
                    width: applyCol.width
                    height: 64
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.lineSoft

                    Row {
                        anchors.fill: parent
                        anchors.margins: Theme.spacingMd
                        spacing: Theme.spacingMd

                        ZAvatar {
                            size: 36
                            name: modelData.name
                            // ⚠ 必须用 peerUid —— 服务端返回的列表里
                            //   我发出的那条 fromUid 是我本人（name 却是对方的）
                            uid: modelData.peerUid
                            online: PresenceController.online[modelData.peerUid] === true
                        }

                        Column {
                            width: parent.width - 36 - parent.spacing - direction.width
                                - Theme.spacingMd
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2

                            Row {
                                spacing: Theme.spacingSm

                                Text {
                                    text: modelData.name
                                    font: Theme.fontStrong
                                    color: Theme.ink
                                }

                                Text {
                                    text: "UID " + modelData.peerUid
                                    font: Theme.fontMonoSm
                                    color: Theme.muted
                                }
                            }
                            Text {
                                width: parent.width
                                text: modelData.verifyInfo.length > 0
                                    ? modelData.verifyInfo : "（无验证信息）"
                                font: Theme.fontBody
                                color: Theme.muted
                                elide: Text.ElideRight
                            }
                        }
                    }

                    // 右侧：方向 + 状态。服务端 state 语义未文档化，原样显示
                    Row {
                        id: direction
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.rightMargin: Theme.spacingMd
                        spacing: Theme.spacingSm

                        ZTag {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.received ? "收到" : "已发出"
                            tone: modelData.received ? Theme.Tone.Accent
                                : Theme.Tone.Muted
                            uppercase: false
                            mono: false
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.stateText
                            font: Theme.fontMonoSm
                            color: Theme.muted
                        }
                    }

                    // 点整行打开申请人资料卡 —— 并带上这条申请本身，
                    // 弹窗会切成"申请详情"形态，不再给「加好友」
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: pane.userInfoRequested(modelData, modelData)
                    }
                }
            }
        }
    }
}
