# qtClient

MyChat 桌面客户端。当前分支 `refactor/zmd-ui-qml` 正在把 UI 层从 Qt Widgets **整体重写为 Qt Quick**
，视觉语言采用 [ZmdUI](third_party/ZmdUI) 组件库。

> 深入设计见 [`ARCHITECTURE.md`](ARCHITECTURE.md)。
> 服务端接口见 `docs/ServerDocs/`（本机笔记，未入版本控制）与 [`接口文档.md`](接口文档.md)。

## 当前状态

| 已完成                                        | 未完成                           |
|-----------------------------------------------|----------------------------------|
| 认证三页：登录 / 注册 / 重置密码，含路由切换  | 历史消息（服务端无接口）         |
| 登录链路（HTTP → TCP 握手 → 切页）+ 免密自动登录 | 群列表 / 退群（服务端未实现） |
| 注册与重置密码链路（邮箱验证码）              | 图片 / 文件消息（服务端无上传通道） |
| 聊天主页：侧栏（会话 / 联系人 / 好友申请）    |                                  |
| 好友链路：搜索用户 / 发申请 / 申请列表        |                                  |
| 好友验证：接受 / 拒绝 + 双向推送（1010–1012） |                                  |
| 好友列表（1020/1021）→ 驱动会话列表           |                                  |
| 删除好友（1027/1028）                         |                                  |
| 好友备注（1029/1030，单向、只对自己可见）     |                                  |
| 编辑本人资料（1031/1032：昵称 / 签名 / 性别） | 头像上传（服务端无接口）         |
| 会话界面：列表 / 气泡版式 / 输入框 / 未读数   |                                  |
| 心跳与在线状态（1016–1019）                   |                                  |
| 发消息（1013/1014，msg_id 回执配对）          |                                  |
| 收消息（1022 推送 → 1023 拉取 → 1025 确认）   | 服务端历史（确认即删）           |
| 本地聊天记录（SQLite，按账号分库）            |                                  |
| 群聊：建群 / 拉人 / 踢人 / 解散 / 成员（1033–1042）+ 群消息 + 系统消息（`from_uid=1`） | 服务端无群列表 / 退群 / 资料变更推送 |
| 群资料：获取 / 修改群名与公告（1043–1046）   | 头像上传（服务端无通道）         |
| core：协议 / 网络 / 存储 / 会话 / 日志        |                                  |
| 十七套无 GUI 的链路自检程序                   |                                  |

## 技术栈

- Qt 6.11.2 + C++20，Qt Quick / QML（ **不链接 QtWidgets**）
- ZmdUI 1.0 组件库（vendor 在 `third_party/ZmdUI`，零图片资源，全 Shape/Canvas 绘制）
- CMake ≥ 3.21 + Ninja，双工具链（MinGW / MSVC）

## 构建

### 命令行

在仓库根执行：

```bash
cmake --preset mingw-debug
cmake --build --preset mingw-debug
```

可用 preset：`mingw-debug`（推荐）、`mingw-release`、`msvc-debug`、`msvc-release`。
产物在 `build/<preset>/`，构建后已自动 `windeployqt`，exe 旁边依赖齐全。

> MSBuild 在本机被安全策略拦截，MSVC 配置只能用 Ninja 生成器 + 手铺环境变量。

### CLion

1. `File > Open` 打开 `C:/dev/MyChat/qtClient`
2. `Settings > Build > CMake` 选 **`mingw-debug`**
3. `Settings > Toolchains` 确认 MinGW（`C:/dev/Qt/Tools/mingw1310_64`）
4. 直接 Run

## 运行

登录需要一个在跑的服务端。本地默认连 `http://localhost:8080`，
可在 `assets/config.ini`（构建后复制到 exe 旁）里改：

```ini
[GateServer]
host=localhost
port=8080
```

登录链路需要 GateServer + ChatServer；注册还需要 Redis / MySQL / VerifyServer
（端口分别是 6380 / 3308 / 50051，全都不是默认值，详见 `ARCHITECTURE.md` §8.2）。

## 调试与自检

```bash
# 无 GUI 跑各条链路（exit 0 = 成功）
qtClientLoginTest    <uid> <password> [gateUrl]
qtClientRegisterTest <email> <nickname> <password> [verifyCode]
qtClientResetTest    <uid> <email> <newPassword> <verifyCode>
qtClientContactTest  <uidA> <passwordA> <uidB> <passwordB>   # 两段式：A 搜 B 加好友 → B 查申请列表

# 自带假服务器、不需要后端的自检（直接跑，无参数）
qtClientFriendDeleteTest    # 1027/1028 删除好友
qtClientProfileEditTest     # 1029-1032 备注 / 编辑资料
qtClientGroupTest           # 1033-1042 群管理 + 群消息 + from_uid=1 系统消息（字段名 / group 布尔 / 群会话键）
qtClientGroupInfoTest       # 1043-1046 群资料获取 / 修改（可选字段语义、缺 error 按失败、保存后重拉）
qtClientMsgIdDropTest       # 请求编号 msgid + 被丢弃请求的识别（约 5s）
qtClientChatStoreTest       # 本地 SQLite 记录

# 群聊探针（真实服务端，扮演另一个群成员）
python tools/probe-group.py <uid> <password> pull [count]
python tools/probe-group.py <uid> <password> send <gid> "文本"
python tools/probe-group.py <uid> <password> members <gid>

# 压测（直连 ChatServer，不经 GateServer 转发）
python tools/bench-chat.py --port 8090

# 启动 GUI 并自动登录
MYCHAT_AUTOLOGIN=<uid>:<password> ./qtClient.exe

# 自动化建群（登录后走真实的 1033 → 1035；成员可省略）
MYCHAT_GROUPCREATE="自检群:666673,666674" ./qtClient.exe

# 无头渲染一帧并抓图（用于自动化视觉验证）
MYCHAT_SMOKE=<输出png> ./qtClient.exe

# 指定起始页，否则只能截到登录页（Login / Register / Reset / Chat）
MYCHAT_PAGE=Register MYCHAT_SMOKE=register.png ./qtClient.exe
```

> 注册 / 重置需要邮箱验证码， **依赖 VerifyServer**。它没启动时验证码请求必然失败，
> 可先 `redis-cli -p 6380 SET code_<email> 123456` 手工写一个来绕过。

**任何一次运行都建议设 `QT_ASSUME_STDERR_HAS_CONSOLE=1`**，否则 Qt 日志走
`OutputDebugString`，重定向后什么都看不到。
详细用法见 [`tools/README.md`](tools/README.md)。

## 目录一览

```
src/         main.cpp、控制器、core/（协议 / 网络 / 会话 / 日志）
qml/         main.qml（路由壳）、pages/（含 pages/chat/ 各分区）、components/（ZAvatar 等自建组件）
resources/   qml.qrc
assets/      config.ini
tests/       无 GUI 自检
tools/       无头抓图用法
third_party/ ZmdUI（vendor）
```

## 分支说明

- `main`：旧的 Qt Widgets 前端（`.vcxproj`），保持可运行、可回退
- `refactor/zmd-ui-qml`：本分支。旧前端源码已从本分支删除，
  需要参考时用 `git show main:<路径>` / `git checkout main -- <路径>`
