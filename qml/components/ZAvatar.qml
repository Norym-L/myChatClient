import QtQuick
import ZmdUI 1.0

/*!
    首字母头像 —— ZmdUI 缺失的组件之一。

    ZmdUI 是「零图片资源」的组件库，没有提供任何头像 / 圆形裁剪 / 图片显示组件，
    而聊天客户端到处都要头像。这里按 ZmdUI 的设计语言补一个：

      · 不依赖任何图片：用昵称首字符（中文取第一个字，英文取首字母大写）
      · 底色按 uid 哈希，保证同一个人每次进来颜色一致
      · 方形 + 轻微斜切，与 ZmdUI 的工业机能风保持一致（不用圆形）

    服务端将来提供头像 URL 后，可以再加一层 Image 覆盖在文字上。
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

    width: size
    height: size

    // 工业风：方形为主，只做很小的斜切，不做圆角
    readonly property color _baseColor: root._pickColor()
    color: _baseColor
    border.width: 1
    border.color: Theme.lineSoft

    /// 按 uid 稳定地选一个底色：同一个人在任何页面都是同一种颜色
    function _pickColor() {
        const palette = [Theme.accent, Theme.cyan, Theme.magenta, Theme.accentSoft]
        if (root.uid.length === 0)
            return palette[0]

        let sum = 0
        for (let i = 0; i < root.uid.length; ++i)
            sum += root.uid.charCodeAt(i)
        return palette[sum % palette.length]
    }

    /// 取首字符：中文取第一个字，其余取首字母大写
    readonly property string _initial: {
        const text = root.name.length > 0 ? root.name : root.uid
        if (text.length === 0)
            return "?"
        return text.substring(0, 1).toUpperCase()
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
    }

    // 右下角在线状态点 —— 复用 ZmdUI 自带的 ZStatusDot
    ZStatusDot {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: -2
        visible: root.online
    }
}
