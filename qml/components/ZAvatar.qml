import QtQuick
import ZmdUI 1.0

/*!
    首字母头像 —— ZmdUI 缺失的组件之一。

    ZmdUI 是「零图片资源」的组件库，没有提供任何头像 / 圆形裁剪 / 图片显示组件，
    而聊天客户端到处都要头像。这里按 ZmdUI 的设计语言补一个：

      · 不依赖任何图片：用昵称首字符（中文取第一个字，英文取首字母大写）
      · 底色按 uid 哈希，保证同一个人每次进来颜色一致
      · 方形 + 轻微斜切，与 ZmdUI 的工业机能风保持一致（不用圆形）

    服务端将来提供头像 URL 后，把 URL 赋给 \l source 即可自动切成真实头像。
*/
Rectangle {
    id: root

    /*! 显示尺寸 */
    property int size: 40
    /*! 昵称；为空时显示 uid */
    property string name: ""
    /*! 用户 id，用于生成稳定底色 */
    property string uid: ""
    /*! 是否在线（右下角状态点） */
    property bool online: false
    /*!
        头像图片地址。为空串时回退到「首字母 + 哈希底色」的占位方案。

        \note 服务端目前没有头像服务（`UserInfo.icon` 恒为空），所以默认走占位；
              等接上上传接口，把返回的 URL 写进来就会显示真实头像 ——
              UserInfoDialog 的 avatarUrl 属性就是给这条链路留的口子。
    */
    property url source: ""
    /*!
        是否响应点击。默认关 —— 头像在很多地方只是"展示"，贸然接上点击会改变
        所在行原本的点击行为（例如搜索结果行整行点击是"选中"）。
    */
    property bool clickable: false

    /*! 头像被点击（clickable 为 true 时才有机会触发） */
    signal clicked()

    width: size
    height: size

    // 工业风：方形为主，只做很小的斜切，不做圆角
    readonly property color _baseColor: root._pickColor()
    color: _baseColor
    border.width: 1
    border.color: Theme.lineSoft

    /// 按 uid 稳定地选一个底色：同一个人在任何页面都是同一种颜色
    function _pickColor() {
        const palette = [Theme.accent, Theme.cyan, Theme.magenta, Theme.accentSoft];
        if (root.uid.length === 0)
            return palette[0];

        let sum = 0;
        for (let i = 0; i < root.uid.length; ++i)
            sum += root.uid.charCodeAt(i);
        return palette[sum % palette.length];
    }

    /// 取首字符：中文取第一个字，其余取首字母大写
    readonly property string _initial: {
        const text = root.name.length > 0 ? root.name : root.uid;
        if (text.length === 0)
            return "?";
        return text.substring(0, 1).toUpperCase();
    }

    Text {
        anchors.centerIn: parent
        text: root._initial
        // ⚠ 不能写 `font: Theme.fontStrong` 再写 `font.pixelSize:` ——
        // QML 里整组赋值后再改子属性会报 "Property has already been assigned a value"。
        // 这里逐字段取，字号按头像尺寸缩放。
        font.family: Theme.fontStrong.family
        font.bold: Theme.fontStrong.bold
        font.pixelSize: Math.round(root.size * 0.45)
        color: Theme.textOnAccent
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        // 有真头像时让位；加载中（status !== Ready）也先显示占位，避免头像位空一块
        visible: !photo.visible
    }

    /*!
        真实头像。盖在占位文字上，靠 visible 切换。

        \note ZAvatar 是方形 + 描边的「工业风铭牌」，不是圆形头像，所以这里
              不做圆形裁剪 —— 图片直接铺满（PreserveAspectCrop 裁掉多余部分），
              内缩 1px 让出描边，形状语言才统一。
    */
    Image {
        id: photo
        anchors.fill: parent
        anchors.margins: 1
        source: root.source
        // 空串 / 加载失败 / 还在下载时都不显示，自动回退到首字母占位
        visible: root.source.toString().length > 0 && status === Image.Ready
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
    }

    // 右下角在线状态点 —— 复用 ZmdUI 自带的 ZStatusDot
    ZStatusDot {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: -2
        visible: root.online
    }

    /*
        只管"我被点了"，不管点了之后干什么 —— 具体弹哪个窗口交给页面。

        ⚠ 千万别在这里 import "../dialog" 去 new 一个 UserInfoDialog：
          UserInfoDialog 的内容里本身就有 ZAvatar，两边互相引用会构成循环依赖，
          QML 直接报 "Cyclic dependency detected between ..."，
          连带 main.qml 一起加载失败（engine.rootObjects() 为空 → 进程立刻退出）。
    */
    TapHandler {
        enabled: root.clickable
        onTapped: root.clicked()
    }

    /*!
        可点击时的鼠标指针反馈。

        \note 用独立的 HoverHandler 而不是给 TapHandler 设 cursorShape ——
              TapHandler 上没有 cursorShape 这个属性（那是 HoverHandler 的），
              写上去会被当成自定义属性，静默不生效。
              HoverHandler 是纯被动处理器，不抢 TapHandler 的点击。
    */
    HoverHandler {
        enabled: root.clickable
        cursorShape: Qt.PointingHandCursor
    }
}
