# qtClient 架构说明

> **适用分支**：`refactor/zmd-ui-qml`
> **形态**：Qt Quick（QML）客户端，基于 ZmdUI 组件库
> **工具链**：C++20 / Qt 6.11.2（MinGW 13.1.0 或 MSVC 14.51）/ CMake + Ninja
> **构建**：CMake Presets，CLion 可直接识别
>
> 本文档描述 **当前代码的实际行为**，不是设计意图。凡与口头描述不一致处，以源码为准。
>
> ⚠ 本分支的旧 Qt Widgets 前端（`.vcxproj` + 根目录 `.cpp/.h/.ui` + `assets/` 图标集）
> **已全部删除**。它们仍完好保存在 `main` 分支，需要参考时用
> `git show main:<路径>` 或 `git checkout main -- <路径>`。

---

## 目录

1. [项目概览](#1-项目概览)
2. [目录结构](#2-目录结构)
3. [架构分层](#3-架构分层)
4. [core 层：协议 / 网络 / 会话 / 日志](#4-core-层协议网络会话日志)
5. [控制器与 QML 桥接](#5-控制器与-qml-桥接)
6. [UI 层](#6-ui-层)
7. [登录链路（端到端数据流）](#7-登录链路端到端数据流)
8. [协议要点与历史陷阱](#8-协议要点与历史陷阱)
9. [构建与运行](#9-构建与运行)
10. [调试入口](#10-调试入口)
11. [已知约束与技术债](#11-已知约束与技术债)

---

## 1. 项目概览

qtClient 是 MyChat 的桌面客户端，负责登录、聊天、联系人与好友生态。当前分支正在 **从 Qt Widgets 整体重写为 Qt Quick**：UI 层由
QML 承载，视觉语言采用 vendored 进来的
ZmdUI 组件库（终末地工业机能风）。

|          | 说明                                                                          |
|----------|-------------------------------------------------------------------------------|
| UI 技术  | Qt Quick / QML（**不是** Widgets，不链接 QtWidgets）                          |
| 组件库   | ZmdUI 1.0，vendor 在 `third_party/ZmdUI`（25 个组件，零图片资源）             |
| 网络     | HTTP（REST，GateServer）+ TCP（长连接帧协议，ChatServer）                     |
| 构建     | CMake ≥ 3.21 + Ninja，**双工具链**（MinGW / MSVC）                            |
| 平台限定 | Windows x64（多处 Windows 专有处理：windeployqt、MinGW 运行库、控制台代码页） |

### 当前已实现

- ✅ **认证三页**：登录 / 注册 / 重置密码，含彼此之间的路由切换
- ✅ 登录链路：本地校验 → HTTP `/user_login` → 连 ChatServer → TCP 登录握手 → 切页
- ✅ 注册链路：请求邮箱验证码 → HTTP `/user_register`
- ✅ 重置密码链路：请求邮箱验证码 → HTTP `/reset_password`
- ✅ 协议 / 网络 / 会话 / 日志四个 core 模块
- ✅ **聊天主页**：左侧 ZSideBar 导航（会话 / 联系人 / 好友申请）+ 右侧分区内容
- ✅ **好友链路**：搜索用户（1003）、发送好友申请（1005）、好友申请列表（1007）
  —— 三条 TCP 消息均已与服务端实测闭环
- ✅ **好友申请推送**（1009 NotifyAddFriendReq）：被申请方在线时服务端主动推，
  客户端收到后自动回拉列表（离线不补推）
- ✅ **好友验证**：接受 / 拒绝收到的申请（1010/1011）、自己发出的申请被处理时的推送
  （1012 NotifyAuthFriendReq）—— 两条链路均已与服务端实测闭环（`api-friend.md`）
- ✅ **好友列表**（1020/1021）：`ContactController.refreshFriendList()` → **会话列表由它驱动**
  （`ChatController.syncFriends()`，在 `main.cpp` 里接线）；备注 `nick` 优先作为显示名
- ✅ **会话界面**（会话列表 / 消息气泡 / 输入框 / 未读角标）—— 版式与交互已就绪，
  会话列表来自好友列表；**收发都已接通服务端**：
  发 1013/1014（`msg_id` 回显配对）、收 1022 → 1023 拉取 → 1025 确认。
  ⚠ 确认后服务端即删除，没有历史记录可翻。
  演示数据只在 `MYCHAT_DEMO=1` 时注入（用于版式自检）
- ✅ 七套无 GUI 的命令行自检程序（复用同一批控制器，验证的是真实链路）

### 尚未实现

- **历史消息**：服务端 `XACK + XDEL`，消息确认即删除，翻不到旧消息
- **群聊**：服务端 `is_to_group` 恒为 false
- 会话持久化（目前关掉进程就要重新登录）

---

## 2. 目录结构

```
qtClient/                      ← 仓库根 == 工程根（不再有子工程层）
├── CMakeLists.txt             构建入口：core / 应用 / 自检三个 target
├── CMakePresets.json          mingw-{debug,release}、msvc-{debug,release}
├── .gitignore
│
├── src/                       应用代码（入口 / 控制器 / 被它们共用的 core）
│   ├── main.cpp                 QGuiApplication + QML 引擎 + 单例注册
│   │
│   ├── AccountValidator.h/.cpp  本地校验规则的**唯一实现**（uid / 邮箱 / 密码 / 昵称 / 验证码）
│   ├── LoginController.h/.cpp         登录链路
│   ├── RegisterController.h/.cpp      注册链路（含验证码 60s 冷却）
│   ├── ResetPasswordController.h/.cpp 重置密码链路
│   ├── ContactController.h/.cpp       联系人 / 好友链路（搜索 / 发申请 / 申请列表 / 推送）
│   ├── ChatController.h/.cpp          会话 / 消息（版式就绪，数据为本地演示）
│   │
│   └── core/                   协议 / 网络 / 会话 / 日志（不依赖外部）
│       ├── protocol.h/.cpp        帧格式、MessageId、ReqId→路径、ErrorCode、parseUid
│       ├── HttpClient.h/.cpp      QNetworkAccessManager 单例
│       ├── ChatSocket.h/.cpp      QTcpSocket 单例 + 帧组包/解包
│       ├── SessionManager.h/.cpp  登录态（uid / name / token）
│       └── logger.h/.cpp          mychat::log，级别过滤与统一格式
│
├── qml/                       QML 页面
│   ├── main.qml                窗口壳 + 路由容器
│   ├── pages/                  LoginPage / RegisterPage / ResetPasswordPage / ChatPage
│   └── components/             自建组件（ZmdUI 缺失、按同一设计语言补的）
│       ├── ZAvatar.qml            首字母头像 + 真实头像 + 状态点
│       ├── ConversationRow.qml    会话列表的一行（头像 / 摘要 / 未读 / 选中态）
│       ├── ChatBubble.qml         消息气泡（自己 / 对方两种朝向 + 投递状态标注）
│       └── ThemeToggle.qml        亮 / 暗主题切换（四页左下角各一份）
│
├── resources/qml.qrc          资源清单（应用 QML + ZmdUI 全部组件）
├── assets/config.ini          GateServer 地址（构建后拷到 exe 旁）
├── tests/                     无 GUI 的链路自检（各自复用对应的控制器）
│   ├── autologin.cpp            登录 → qtClientLoginTest
│   ├── registerflow.cpp         注册 → qtClientRegisterTest
│   ├── resetflow.cpp            重置密码 → qtClientResetTest
│   └── contactflow.cpp          好友链路 → qtClientContactTest
├── tools/README.md            无头自检（抓图）用法
├── third_party/ZmdUI/         vendor 的组件库（升级时整目录替换，勿改其中文件）
│
├── docs/                      ⚠ **未纳入版本控制**（见 .gitignore 的 /docs/）
│   └── ServerDocs/            api-http / api-tcp / api-grpc / error-codes
├── 接口文档.md                服务端 JSON 字段约定（好友、搜索等，后续页面要用）
└── static/head_1.jpg          头像占位图
```

> `docs/` 被 `.gitignore` 显式排除 —— 它是 **本机笔记**，删掉就找不回来，别去动它。

---

## 3. 架构分层

```
┌──────────────────────────────────────────────────────────┐
│  QML  (qml/)                                             │
│  只调控制器方法、读属性、接信号                              │
│  import MyChat.Core 1.0                                  │
└──────────────────┬───────────────────────────────────────┘
                   │  Q_INVOKABLE / Q_PROPERTY / signals
┌──────────────────▼───────────────────────────────────────┐
│  控制器 (src/LoginController)                             │
│  编排整条业务链路；本地校验规则在此唯一实现                   │
└──────────────────┬───────────────────────────────────────┘
                   │
┌──────────────────▼───────────────────────────────────────┐
│  core  (HttpClient / ChatSocket / SessionManager / log)   │
│  REST、TCP 帧协议、会话态、日志                              │
└──────────────────┬───────────────────────────────────────┘
                   │
              Qt6::Core / Qt6::Network
```

三条硬规则：

1. **QML 不直接碰 core。** `HttpClient` / `ChatSocket` 的接口形态是
   `QUrl` / `QJsonObject` / `MessageId` 枚举，直接暴露给 QML 既难用，
   也等于把「uid 必须按字符串传」这类协议约束交给 QML 去守。
2. **本地校验只有 C++ 一份。** 账号 / 密码正则写在 `LoginController`，QML 调用它，
   不要另写一份（历史上因为两边正则不一致踩过坑）。
3. **QML 不 import 仓库外的模块。** `src/core` 自成一体，命名空间 `mychat`。

### CMake target

| target                 | 产物         | 用途                                   |
|------------------------|--------------|----------------------------------------|
| `qtclient_core`        | 静态库       | core + 控制器 + 校验器，被下面几个共用 |
| `qtClient`             | GUI 可执行   | 客户端本体                             |
| `qtClientLoginTest`    | 控制台可执行 | 无 GUI 跑登录链路                      |
| `qtClientRegisterTest` | 控制台可执行 | 无 GUI 跑注册链路                      |
| `qtClientResetTest`    | 控制台可执行 | 无 GUI 跑重置密码链路                  |
| `qtClientContactTest`  | 控制台可执行 | 无 GUI 跑好友链路（两段式 A→B）        |

自检程序链接的是 **同一个** `qtclient_core`，所以不存在「测试另写一套逻辑」的问题。

---

## 4. core 层（协议 / 网络 / 会话 / 日志）

### 4.1 protocol

**TCP 帧格式**（手工拼大端字节，不依赖 `QDataStream` 的隐式行为）：

```
+----------------+----------------+----------------------------+
| message_id     | body_len       | body                       |
| 2 字节，大端序  | 2 字节，大端序  | body_len 字节，UTF-8 JSON  |
+----------------+----------------+----------------------------+
```

- `HEAD_ID_LEN = 2`、`HEAD_DATA_LEN = 2`、`MAX_BODY_LEN = 2048`（**出站**上限）
- **出站**（发请求）body 超过 2048 → 服务端 `clearSession`，所以发送前必须自查
- **入站不做长度校验**：`body_len` 是 2 字节字段，物理上限 65535；服务端 `SendNode`
  写帧头时不校验长度，好友列表（1021）随好友数量增长很容易超过 2048。
  客户端曾拿 2048 卡入站，结果是"好友一多就断线、界面只说连接已断开"（已修）。
- 只对 `body_len` 做上限校验 —— **`message_id` 是枚举不是长度**，拿它和
  `MAX_BODY_LEN` 比毫无意义，且会把 >2047 的新消息号误杀

**MessageId**：`ChatLoginReq = 1001`、`ChatLoginRsp = 1002`，其后各项递增
（好友 / 认证 / 文本消息等共 15 个）。取值以 **服务端实现**为准，参见 §8.1。

**ErrorCode**：0 成功；1~4 客户端本地码；1001 起与 `docs/ServerDocs/error-codes.md`
一致。HTTP / gRPC / TCP 三种响应共用同一套。

**为什么要 `parseUid`**：uid 一律以 **uint64 的字符串**形式传递。
`QJsonValue` 内部用 double 存数字，只有 53 位有效整数，超过 `2^53` 会静默丢精度。
为兼容旧服务端仍接受数字形式，但超出 double 精确范围时会记 WARN。

**ReqId → 路径**：HTTP 路径集中在 `protocol::reqPath()` 一处，各控制器不再各写一份
字符串字面量。注意 `/get_Verifycode` 的大小写就是服务端实现的写法（其余接口全小写），
不要顺手统一。

**`verifyCodeErrorToTip`（容易踩）**：`/get_Verifycode` 的 `error` 语义与别的接口 **不同** ——
GateServer 把 VerifyServer 的 `rsp.error()` **原样透传**（`root["error"] = rsp.error()`），
所以非零值往往是个小数字（如 `1`），并不是 ErrorCode 里的 1001/1002。
套 `errorToTip` 会把它误报成「服务器返回数据解析失败」，排查时严重带偏方向，
因此这个接口单独走 `verifyCodeErrorToTip`。

### 4.2 HttpClient

`QNetworkAccessManager` 单例，`setBaseUrl()` 后 `post(path, json, callback)`。
统一把失败包装成 `{ "error": <ErrorCode> }` 回给调用方，业务层不需要关心网络细节。

注意： **业务失败也是 HTTP 200**，必须再看 body 里的 `error`。

### 4.3 ChatSocket

`QTcpSocket` 单例。职责边界：

- `connectToServer(host, port)` —— 带超时定时器
- `login(uid, token)` —— 发送 `ChatLoginReq`
- `searchUser` / `sendFriendRequest` / `friendApplyList` —— 好友链路三条消息
- `sendJson(message_id, body)` —— 组帧发送
- `dispatchBuffer()` —— 循环解帧（粘包 / 半包都在这里处理）
- `handleFrame()` —— 按编号分发；已知回包全部有处理，服务端推送 1009 / 1012 也在其中，
  真正未知的编号打 WARN 但不断开连接

连接超时、socket 错误、对端断开分别发出 `connectFailed` / `linkError` 信号。

**回包超时是统一的**：每发一个请求就 `expectReply(请求号, 回包号, 超时回调)` 登记一笔，
回包到了由 `handleFrame` 最前面的 `finishReply()` 销账，到期没回就按 `RequestTimeout`
（客户端本地码 5，文案「网络连接超时，请稍后重试」）回调一次。

这么做的原因不是洁癖 —— 服务端 **确实存在不回包的分支**（实测：拿一条不属于自己的
申请去发 1010，服务端一句都不回）。TCP 层没有任何"一定有回包"的保证，缺了这道闸门
的表现是界面永远转圈、后续请求全被"上一个还没回包"挡掉，而且完全静默。

因此新增消息时 **必须**在发送处调一次 `expectReply()`；`tests/sockettimeout.cpp`
用一个只收不发的哑服务器把这件事钉住（不需要真实服务端）。

### 4.4 SessionManager

保存 `uid` / `name` / `token`，提供 `label()` 生成 `"UID 666672 · agentnjqi"`
这样的显示串。目前只在登录成功后写入， **尚未持久化到磁盘**。

### 4.5 logger

`mychat::log::init()` 在 `main()` 最前面安装 Qt 消息处理器。输出格式：

```
[22:45:00.318] [INFO ] [login] HTTP 登录成功: uid = 666672 host = "127.0.0.1" port = 8090
```

级别由 CMake 按构建配置控制。别指望 Qt 默认日志在 CLion / 重定向里可见 —— 见 §10。

---

## 5. 控制器与 QML 桥接

### 5.1 注册方式

```cpp
LoginController login_controller;                     // 必须在 engine 之前声明
qmlRegisterSingletonInstance("MyChat.Core", 1, 0, "LoginController", &login_controller);
QQmlApplicationEngine engine;
```

**控制器必须比 `QQmlApplicationEngine` 活得久**，所以在 `main()` 里先声明、
engine 后声明。QML 侧 `import MyChat.Core 1.0` 后直接用 `LoginController.xxx`。

三个认证控制器（`LoginController` / `RegisterController` / `ResetPasswordController`）
在 `main()` 里同时注册为 `MyChat.Core` 模块的单例。

### 5.2 校验规则的唯一实现 —— AccountValidator

`src/AccountValidator.h/.cpp` 提供 `checkUid` / `checkEmail` / `checkPassword` /
`checkNickname` / `checkVerifyCode`， **全部返回空串表示通过**。

集中在一处是硬性要求：登录、注册、重置都要校验邮箱和密码，各自写一份正则迟早出现
「本地放行、服务端拒绝」的错位 —— 这类 bug 表现随机、极难定位。
`LoginController::CheckAccount/CheckPassword` 只是转发到这里。

### 5.3 LoginController

| 类型          | 名称                       | 说明                                            |
|---------------|----------------------------|-------------------------------------------------|
| `Q_INVOKABLE` | `login(account, password)` | 发起整条登录链路                                |
| static        | `CheckAccount(text)`       | 转发到 `AccountValidator`                       |
| static        | `CheckPassword(text)`      | 同上                                            |
| `Q_PROPERTY`  | `tip` / `tipIsError`       | 当前提示文案 / 是否错误（错误显示成红色）       |
| `Q_PROPERTY`  | `busy`                     | 请求进行中，用于禁用输入与按钮                  |
| `Q_PROPERTY`  | `userLabel`                | 登录后形如 `"UID 666672 · agentnjqi"`           |
| signal        | `validationFailed(field)`  | 本地校验失败，`field` 为 `account` / `password` |
| signal        | `loginSucceeded()`         | 整条链路成功，触发路由切页                      |
| signal        | `loginFailed(reason)`      | 非校验类失败（网络 / 密码错 / 握手失败…）       |

> `CheckAccount` / `CheckPassword` 是 static， **不能加 `Q_INVOKABLE`** ——
> 元对象系统不会为 static 成员函数生成方法表项，加了 QML 也访问不到。
> 这两个目前只给 C++ 侧用（自检程序），QML 通过 `login()` 触发校验。
>
> `loginFailed` 不是装饰：没有它，调用方只能靠 `busy` 变化 + `tip` 猜状态，
> 自检程序遇到登录失败会一路干等到超时（实测一次 20 秒）。

### 5.4 RegisterController / ResetPasswordController

两者结构对称，都有 `requestVerifyCode(email)` + 各自的提交方法：

| 控制器                    | 提交方法                                                       | 对应接口          |
|---------------------------|----------------------------------------------------------------|-------------------|
| `RegisterController`      | `registerUser(email, nickname, password, confirm, verifyCode)` | `/user_register`  |
| `ResetPasswordController` | `resetPassword(uid, email, newPassword, verifyCode)`           | `/reset_password` |

共同点：

- `cooldown` 属性 —— 请求验证码后进入 **60 秒冷却**，QML 据此禁用按钮并显示倒计时
- `verifyCodeRequested()` / `validationFailed(field)`
- 验证码错误走 `verifyCodeErrorToTip`（见 §4.1，这个接口的 error 语义特殊）

两个坑（都已修，但改代码时容易再踩）：

1. **方法名不能叫 `register`** —— `register` 是 C++ 保留字。
2. **`setBusy(false)` 必须放在回调的最后**。它会发 `busyChanged`，订阅方可能在回调里
   立刻发起下一步（`qtClientRegisterTest` 就是这么写的）；提示若还没写好，
   就会被下一步的提示覆盖，用户看到的是上一步的信息。

### 5.5 ContactController

联系人 / 好友链路的唯一入口，覆盖服务端 **已实现** 的五条 TCP 消息：

| 方法                                           | 对应消息                                | 说明                                                     |
|------------------------------------------------|-----------------------------------------|----------------------------------------------------------|
| `searchUser(keyword)`                          | `SearchUserReq/Rsp`（1003/1004）        | keyword 可为 uid 或用户名，服务端两种都认                |
| `sendFriendRequest(toUid, verifyInfo, remark)` | `AddFriendReq/Rsp`（1005/1006）         | toUid 按字符串传                                         |
| `refreshApplyList()`                           | `SyncFriendRequestReq/Rsp`（1007/1008） | 拉取「别人发给我的」申请列表                             |
| `handleFriendRequest(requestId, state)`        | `AuthFriendReq/Rsp`（1010/1011）        | `state` 1 接受 / 2 拒绝；1011 回显编号但不回显 state     |
| `refreshFriendList()`                          | `SyncFriendListReq/Rsp`（1020/1021）    | 请求体发 `{}`（服务端不解析，取会话 uid）；见下方 5.5 注 |
| `clearSearchResult()`                          | ——（本地）                              | 清空搜索态                                               |

推送（服务端主动，无回包）：`NotifyAddFriendReq`（1009，有人申请加你）、
`NotifyAuthFriendReq`（1012，你发出的申请被处理）。两者收到后都会自动回拉一次列表， **但补拉是静默的** —— 不会用"共 N
条"盖掉"对方接受了你的申请"这类真正要看的提示。

属性：`tip` / `tipIsError` / `busy`、搜索结果 `hasSearchResult` / `searchUid` /
`searchName` / `searchDesc` / `searchSex`、申请列表 `applyList`（`QVariantList`，
每项含 `fromUid` / `name` / `verifyInfo` / `state`，供 Repeater 直接用）、
好友列表 `friendList` / `friendCount`（每项含 `uid` / `name` / `desc` / `nick` /
`displayName` / `sex` / `icon`）。

⚠ `1021` 的三个实测坑：`count` 是 **字符串**；没有好友时 `friends` 是 **null**（不是缺字段）；
好友资料查不到的那条服务端会跳过（count 可能大于条数）。解析全在
`protocol::parseFriendList()` 里兜住，不要在校验层重写一遍。

信号：`searchFinished(found, error)`（未找到通常是 1014）、
`friendRequestSent(ok, error)`、`validationFailed(field)`。

⚠ 好友列表（1020/1021）走本类而不是 ChatController：好友是 **联系域**数据，
会话列表只是它的投影（`main.cpp` 把 `friendListChanged` 接到 `ChatController::syncFriends`）。
收到的 `friends` 里 `nick` 是我给对方的备注，C++ 已算好 `displayName`（备注优先）。

### 5.6 ChatController

会话 / 消息的唯一入口。 **会话列表由好友列表驱动**（`syncFriends()`）；
收发都已接通服务端：发 1013/1014（本地落一条 `sending` 气泡 → 回执按 `msg_id` 改状态），
收 1022 推送 → 1023 拉取 → 1024 落库 → 1025 确认。历史仍为空（服务端确认即删除）。
`MYCHAT_DEMO=1` 时才注入演示会话（`demo: true`，`syncFriends` 不会清掉它们）。

| 方法                            | 用途                                                             |
|---------------------------------|------------------------------------------------------------------|
| `openConversation(uid, name)`   | 打开会话（顺带清未读）；没有选中时页面自动开第一条               |
| `syncFriends(friends)`          | 用好友列表重建会话：资料字段取新的，未读/摘要沿用旧的            |
| `sendText(text)` → bool         | 落一条 `sending` 气泡并发 1013；空串/超长返回 false              |
| `markRead(uid)`                 | 清未读角标                                                       |
| `appendIncoming(uid, text, t)`  | 单条注入（调试用）；真正的收消息走 `onChatMessages()` 批量路径   |
| `markMessageState(uid, id, st)` | 改某条气泡的状态（`sending` / `sent` / `failed`）                 |

属性：`conversations` / `conversationCount` / `messages` / `hasActiveConversation` /
`activePeerUid` / `activePeerName` / `activePeerIsDemo` / `tip` / `tipIsError`。

**收消息链路的关键约定**（`api-chat.md` §3.1 + 2026-10-18 晚的 PEL 重投）：

1. 1022 只是"有新消息"的信号（**不含内容**）→ 必须自己发 1023 拉。
2. 节流：攒够 **10 条以上**立刻拉；否则等 **2s** 合并成一批。计时器**不因每条推送重启**
   （持续来消息时会永远推后，永远拉不成）。
3. `count = min(待拉取计数, 10)` —— 服务端单次上限 10，超了回 `1020`；`1024` 的 `count`
   是**字符串**（`to_string`）。
4. **服务端 PEL 优先**：先重投"已投递未确认"的消息，PEL 空才读新消息 →
   **同一条消息会被重复下发（at-least-once）**，客户端必须按 `msgs[].id` 去重
   （`m_seen_message_ids`，内存态、上限 2000 条）。
5. **去重 ≠ 跳过确认**：重复的那条也要放进 1025 的 `ids` 里，否则服务端会一直重投。
6. `msgs[].id` 是 Redis Stream ID（`<毫秒>-<序号>`），既用于确认，也是客户端唯一的
   "消息时间"来源（服务端不回时间戳）—— `ChatController::timeFromStreamId()`。
7. 回来的条数**达到本次请求的 count**（不是硬编码 10）→ 200ms 后再拉一批，
   直到拉不满为止；PEL 抽干后才算停。
8. **登录成功后主动拉一次**：离线期间的消息不会补推 1022，PEL 里上次没确认的也靠这一步找回来。
9. 拉取超时 → 2s 后自动重试（消息还在服务端）；确认失败 → 1s 后再拉（PEL 会重投），
   连续失败 3 次就停手，别对着不健康的服务端空转。
10. 迟到（已超时）的 1024 也照常处理：反正下一轮 PEL 还会重投，能早一步是一步。

**发送链路的关键约定**：

1. **`msg_id` 由本类生成**（`c-<本地气泡 id>`），服务端原样回显在 1014 里 ——
   回执靠它精确对回气泡，不依赖发送顺序。别在别处自己拼 msg_id 发 1013。
2. `ChatSocket` 仍然**串行发送**：它的 `m_pending` 超时表按回包型号 FIFO 认领，
   并发时先回的那条会销掉别人的账。
3. `error == 0` 只代表"已写入对方离线消息流"，**不是**"对方已收到"，文案别写"已送达"。
4. 断线时 `ChatSocket` 直接清空队列（不补报超时），所以 `ChatController` 监听
   `linkError` 把 `m_sending` 里的气泡统一标成 `failed`。

⚠ **`syncFriends()` 里的信号重入陷阱**：`emit conversationsChanged()` 之后 QML 会 **同步**
自动打开第一个会话（`ChatPage` 的 handler），所以"原来有没有活动会话"必须在 emit **之前**
记下来。之前写成 emit 之后再判断，结果订阅方刚打开就被 `closeConversation()` 关掉 ——
现象是列表里有好友、右侧停在「未选择会话」，日志完全正常。

⚠ **QML 绑定的坑**：`PresenceController.hasState(uid)` 这类 **函数调用**在 QML 眼里是黑盒，
属性变了也不会重算绑定（曾表现为日志里明明回了 offline、界面一直停在"状态未知"）。
需要依赖时把 `PresenceController.online` 当 **参数**传进函数（见 `ChatPage.peerStateText`）。

### 5.7 路由

`qml/main.qml` 用 `Loader` 切换 `sourceComponent`：

```
LoginPage ──loginSucceeded──▶ ChatPage
          ──registerRequested──▶ RegisterPage ──backRequested──▶ LoginPage
          ──resetRequested─────▶ ResetPasswordPage ──backRequested──▶ LoginPage
ChatPage  ──logoutRequested──▶ LoginPage
```

相比旧前端 `setCentralWidget` + `deleteLater` 的做法，`Loader` 卸载即断开连接，
天然避免了「旧页面继续响应全局信号」的坑。

---

## 6. UI 层

### 6.1 ZmdUI

`third_party/ZmdUI/` 是 vendor 进来的 **纯 QML** 组件库（`ae2bff2`，25 个组件）。约定：

- 升级时 **整目录替换**，不要手改其中的文件（否则无法 diff 上游）
- 资源组织沿用 ZmdUI 自身惯例：普通 `.qrc` + `engine.addImportPath("qrc:/qml")`，
  模块根 `:/qml/ZmdUI`（内含 `qmldir`）， **不用 `qt_add_qml_module`**
- 必须 `QtQuick.Controls` 的 **Basic** 样式：ZmdUI 完全自定义了 background /
  contentItem / indicator，平台原生样式介入会出现圆角、蓝色高亮等冲突绘制

### 6.2 当前用到的组件

| 页面                | 组件                                                                                                                                                         |
|---------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `LoginPage`         | `ZPanel`、`ZTextField`、`ZButton`、`ZGridBackground`                                                                                                         |
| `RegisterPage`      | 同上                                                                                                                                                         |
| `ResetPasswordPage` | 同上                                                                                                                                                         |
| `ChatPage`          | `ZSideBar`、`ZPanel`、`ZTextField`、`ZButton`、`ZSectionTitle`、`ZTag`、`ZIconButton`、`ZGridBackground` + 自建 `ZAvatar` / `ConversationRow` / `ChatBubble` |

用到的 Theme 令牌：`bg` `surface` `ink` `inkSoft` `muted` `accent` `danger` `lineSoft`
`gridLine` `gridLineMajor` `fontBody` `fontStrong` `fontMonoMd` `fontMonoSm` `spacingSm`
`spacingMd` `spacingLg` `Variant.*`。

**业务 QML 里不写魔法数字和硬编码颜色**，一律取 `Theme`。

### 6.3 主题切换

`Theme` 是 singleton，所有颜色都是 `Theme.isDark ? A : B` 的 **绑定**，所以换肤
只需要改一个值：`Theme.mode = Theme.Mode.Dark | Theme.Mode.Light`。

`qml/components/ThemeToggle.qml` 做的就是这件事，外加把选择存起来
（`QtCore` 的 `Settings`，键 `Theme/mode` → Windows 上落到注册表
`HKCU\Software\MyChat\MyChat`）。四个页面 **各挂一份**，共用一个键，所以任意一处切换
另外几处跟着变。

⚠ 别用 `Qt.labs.settings` 那个 `Settings`：Qt 6.11 会打印
"deprecated ... use the one from QtCore"，而且它会多依赖一个 QML 插件（要额外部署）。

自动化：`MYCHAT_THEME=light|dark` 在 **启动前**把存档改掉（写的是同一份存档，
不是另开状态），所以无头截图验的就是用户点出来的那套。见 `tools/README.md`。

---

## 7. 链路（端到端数据流）

### 7.1 登录

```
LoginPage                LoginController                  core                服务端
   │                           │                            │                    │
   │ login(account, password)  │                            │                    │
   ├──────────────────────────▶│                            │                    │
   │                           │ CheckAccount/Password      │                    │
   │  validationFailed(field)  │（失败则直接返回）            │                    │
   │◀──────────────────────────┤                            │                    │
   │                           │ POST /user_login           │                    │
   │                           ├───────────────────────────▶│─────HTTP────────▶  │
   │                           │                            │◀──200 {error:0,    │
   │                           │◀───────────────────────────┤     uid, token,    │
   │                           │                            │     host, port}────│
   │                           │ connectToServer(host,port) │                    │
   │                           ├───────────────────────────▶│════TCP 连接═════▶  │
   │                           │◀───── connected ───────────┤                    │
   │                           │ login(uid, token)          │                    │
   │                           ├───────────────────────────▶│─1001 {uid,token}▶  │
   │                           │                            │◀─1002 {error:0,    │
   │                           │◀──── loginSucceeded ───────┤     name,...}──────│
   │                           │ SessionManager.update()    │                    │
   │  loginSucceeded()         │                            │                    │
   │◀──────────────────────────┤                            │                    │
   │  Loader 切到 ChatShellPage│                            │                    │
```

任一步失败都会通过 `tip` / `tipIsError` 呈现给用户，并结束 `busy` 态。
GateServer 会在 8090 / 8091 两个 ChatServer 之间轮转分配，返回哪个就连哪个。

### 7.2 注册

```
RegisterPage              RegisterController             core            服务端
   │                            │                         │                │
   │ requestVerifyCode(email)   │                         │                │
   ├───────────────────────────▶│ 校验邮箱                 │                │
   │                            ├────────────────────────▶│─/get_Verifycode▶│
   │                            │◀────────────────────────┤◀───error/码────│
   │  cooldown = 60s            │ （成功则开始冷却）        │                │
   │◀───────────────────────────┤                         │                │
   │                            │                         │                │
   │ registerUser(email, nick,  │                         │                │
   │   pwd, confirm, code)      │                         │                │
   ├───────────────────────────▶│ 校验 5 项 + 两次密码一致  │                │
   │                            ├────────────────────────▶│─/user_register─▶│
   │                            │◀────────────────────────┤◀─{error,uid}───│
   │  registerSucceeded(uid)    │                         │                │
   │◀───────────────────────────┤                         │                │
   │  提示 2 秒后自动回登录页    │                         │                │
```

### 7.3 重置密码

与注册的差别只有一个：要额外填 **uid**。服务端靠 `(uid, email)` 配对校验
（不匹配返回 `1007`），只给邮箱不够。提交走 `/reset_password`。

⚠ 该接口的 `user` 字段服务端 **按字符串解析**（`LogicSystem` 里 "invalid uid string"
那条日志），所以照字符串提交，与登录保持一致。

### 7.4 好友链路（TCP）

```
ChatPage                ContactController              ChatSocket            ChatServer
   │                         │                            │                     │
   │ searchUser(keyword)     │                            │                     │
   ├────────────────────────▶│ 本地校验 keyword            │                     │
   │                         ├───────────────────────────▶│─1003 {uid,name}───▶│
   │                         │◀───────────────────────────┤◀─1004 {error,       │
   │  搜索结果卡               │                            │    uid,name,sex}───│
   │◀──hasSearchResult───────┤                            │                     │
   │                         │                            │                     │
   │ sendFriendRequest(uid,  │                            │                     │
   │   verifyInfo, remark)   │                            │                     │
   ├────────────────────────▶│ 本地校验 toUid              │                     │
   │                         ├───────────────────────────▶│─1005 {uid,touid,    │
   │                         │                            │    verifyinfo}────▶│
   │                         │◀───────────────────────────┤◀─1006 {error}──────│
   │  friendRequestSent(ok)  │                            │                     │
   │                         │                            │                     │
   │ refreshApplyList()      │                            │                     │
   ├────────────────────────▶├───────────────────────────▶│─1007 {uid}────────▶│
   │                         │◀───────────────────────────┤◀─1007 {applylist:   │
   │  applyList 更新          │                            │    [{from_uid,name, │
   │◀──applyListChanged──────┤                            │     verifyinfo}...}]│
```

实测闭环：账号 666674 搜索 666672 → 发申请 → 666672 端 `applyList` 收到该条申请，
GUI 抓图（`MYCHAT_AUTOLOGIN + MYCHAT_SMOKE`）确认渲染正常。

---

## 8. 协议要点与历史陷阱

### 8.1 ⚠ TCP 的 message_id 以「实现」为准，不是文档

| 来源                                   | 登录请求编号          |
|----------------------------------------|-----------------------|
| `docs/ServerDocs/api-tcp.md`           | `0`（**文档写错了**） |
| 服务端 `GateServer/src/public/const.h` | `1001`                |
| 本客户端 `src/core`                    | `1001`                |

发错的症状极其隐蔽：服务端打印 `dealMessage() unknow message id` 后 **既不回包也不断开连接**，客户端表现为永久等待。 **TCP
收不到任何响应时，第一件事就是查 message_id 取值。**

建议改文档，不要改实现（改实现会破坏旧客户端）。

### 8.2 本地服务端拓扑（端口全不是默认值）

| 服务                 | 端口        |
|----------------------|-------------|
| GateServer（HTTP）   | 8080        |
| ChatServer 1 / 2     | 8090 / 8091 |
| StatusServer（gRPC） | 50052       |
| VerifyServer（gRPC） | 50051       |
| MySQL                | 3308        |
| Redis                | 6380        |

- **VerifyServer 经常忘记启动** → `/get_Verifycode` 返回
  `{"error":1,"verify_code":"there is no verify in redis!"}`。
  绕过办法：手工往 Redis 执行 `SET code_<email> 123456` 再调 `/user_register`。
- 判断 Redis 是否在跑，用 `redis-cli ping` 比 `netstat` 可靠
  （`netstat | grep ":6380 "` 会漏判）。
- `/user_register` 的 `user` 字段是 **昵称**，不是 uid。

### 8.3 uid 一律字符串化

见 §4.1。凡是 JSON 里出现 uid（请求或响应），都按字符串处理。

---

## 9. 构建与运行

### 9.1 用 CLion（推荐）

1. `File > Open` 打开 **`C:/dev/MyChat/qtClient`**（现在仓库根本身就是工程根）
2. `Settings > Build > CMake` 选 **`mingw-debug`** preset
3. `Settings > Toolchains` 确认 MinGW（`C:/dev/Qt/Tools/mingw1310_64`）
4. 直接 Run —— 构建后已自动 `windeployqt`，exe 旁边依赖齐全

### 9.2 命令行（不依赖 CLion，需在仓库根执行）

```bash
cmake --preset mingw-debug
cmake --build --preset mingw-debug
```

MSVC 那套在本机 **必须**先铺环境变量（`INCLUDE` / `LIB` / `PATH`），且只能用 Ninja
生成器 —— MSBuild 被安全策略硬拦。

### 9.3 构建系统里几个「看着多余但删了就崩」的点

| 点                                       | 为什么必须有                                                                                                                                                                                                                           |
|------------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| `set(CMAKE_AUTORCC ON)`                  | `qt_standard_project_setup()` **只开 AUTOMOC / AUTOUIC，不开 AUTORCC**。漏了 `.qrc` 会被静默忽略：配置/编译/链接全绿、零警告，但运行时报 `qrc:/qml/main.qml: No such file or directory`，`main()` 返回 -1（CLion 里就是"退出代码 -1"） |
| 构建后 `windeployqt`                     | Windows 没有 RPATH，CLion 的 Run 不会帮你加 Qt 的 bin。缺这步一 Run 就 `0xC0000135`                                                                                                                                                    |
| windeployqt 要显式给 `--qmldir`          | 本工程用普通 `.qrc` 而非 `qt_add_qml_module`，部署工具不会自动知道 QML 在哪，漏了会导致 `qml/` 目录为空、程序起不来                                                                                                                    |
| 只有 Release 的套件要传 `--release`      | `Qt 6.11.2/mingw_64` **只有 Release 库**（无 `Qt6Cored.dll`）。Debug 构建链接的也是 Release Qt，给 `--debug` 会去找不存在的 `qwindowsd.dll` 直接失败。CMake 会自动探测套件再决定参数                                                   |
| MinGW `-static-libgcc -static-libstdc++` | MinGW 产出的 exe 默认依赖 `libstdc++-6.dll` / `libgcc_s_*.dll`，这两个既不在 Qt 里、windeployqt 也不会拷。`libwinpthread-1.dll`（线程模型 DLL）仍动态，构建后自动拷到 exe 旁。可用 `objdump -p qtClient.exe \| grep "DLL Name"` 复核   |
| 编译器家族与 Qt 套件匹配的 `FATAL_ERROR` | MinGW 配 MSVC 版 Qt（或反之）能编过，运行时必崩，且报错信息离根因很远                                                                                                                                                                  |

### 9.4 可选开关

| 开关                            | 默认 | 作用                                         |
|---------------------------------|------|----------------------------------------------|
| `QTCLIENT_CONSOLE`              | OFF  | 保留控制台窗口（调试 `qDebug` 时开）         |
| `QTCLIENT_STATIC_MINGW_RUNTIME` | ON   | MinGW 下静态链接 GCC 运行库                  |
| `QTCLIENT_DEPLOY_TO_BUILD_DIR`  | ON   | 构建后部署 Qt 运行时到构建目录               |
| `QTCLIENT_WARNINGS`             | ON   | MSVC `/W4 /permissive-`；GCC `-Wall -Wextra` |

---

## 10. 调试入口

```bash
# 1) 无 GUI 跑各条链路（退出码 0 成功 / 2 参数错 / 3 失败 / 4 超时）
qtClientLoginTest    <uid> <password> [gateUrl]
qtClientRegisterTest <email> <nickname> <password> [verifyCode]
qtClientResetTest    <uid> <email> <newPassword> <verifyCode>
qtClientContactTest  <uidA> <passwordA> <uidB> <passwordB>

# 不需要服务端：回包超时兜底（自带只收不发的哑服务器，0 = 五条请求全都自己超时收尾）
qtClientSocketTimeoutTest

# 2) 启动 GUI 并自动登录
MYCHAT_AUTOLOGIN=<uid>:<password> ./qtClient.exe

# 3) 无头渲染一帧并抓图落盘（0 成功 / 3 根对象不是 Window / 4 保存失败）
MYCHAT_SMOKE=<输出png> ./qtClient.exe

# 4) 指定起始页 —— 否则只能测到登录页（取值 Login / Register / Reset / Chat，大小写不敏感）
MYCHAT_PAGE=Register MYCHAT_SMOKE=register.png ./qtClient.exe

# 5) 二者合用：等「登录 → 切页」完成后再抓聊天页（等待自动放宽到 4s）
MYCHAT_AUTOLOGIN=<uid>:<password> MYCHAT_SMOKE=chat.png ./qtClient.exe
```

详细用法见 `tools/README.md`。

> 注册 / 重置的自检需要验证码。 **VerifyServer 没启动时验证码请求会失败**，
> 先用 `redis-cli -p 6380 SET code_<email> <六位码>` 手工写一个即可绕过（见 §8.2）。

### ⚠ 必设的环境变量

```
QT_ASSUME_STDERR_HAS_CONSOLE=1
```

不设的话 Qt 的日志走 `OutputDebugString`， **重定向到文件什么都看不到**，
很容易把「没输出」误读成「没错误」。

同样重要的是： **判断 GUI exe 是否真的起来，绝不能只看进程是否存活** ——
QML 加载失败时 Qt 会弹错误对话框，进程照样活着。必须读 stderr 判断有没有
`failed to load component` / `is not installed`。

---

## 11. 已知约束与技术债

1. **认证 + 好友链路 + 好友列表 + 收发消息已完成；历史 / 群聊未做。** 会话列表由好友列表驱动，
   发 1013/1014（`msg_id` 回显配对）、收 1022 → 1023 → 1025（`XACK + XDEL`）。
   ⚠ 消息**确认即删除**，所以每个会话只能看到"进程启动后收到 / 发出"的那些，
   翻不到更早的记录。后续可参考 `docs/ServerDocs/api-chat.md`。

2. **`ChatSocket::handleFrame` 的 default 分支是预留扩展点。**
   目前处理了登录 / 搜索 / 发申请 / 申请列表 / 好友列表 / 心跳 / 查在线 / 发消息 /
   新消息推送 / 拉取 / 确认，未知编号打 WARN 不断连。

2.1 **注册 / 重置密码依赖 VerifyServer。** 该服务未启动时 `/get_Verifycode`
必然失败，属于环境问题而非客户端 bug —— 报错信息里已区分（见 §4.1）。

3. **ZmdUI vendor 版本 `ae2bff2`。** 升级流程：
   ```bash
   diff -rq C:/dev/ZmdUI/qml/ZmdUI third_party/ZmdUI   # 看差异
   cp -r C:/dev/ZmdUI/qml/ZmdUI/. third_party/ZmdUI/    # 整目录替换
   ```
   ZmdUI 位于 `C:\dev\ZmdUI`，查远程用 **只读**的 `git ls-remote origin`，
   别在那里做 fetch / pull 等写操作。

4. **会话未持久化。** 关掉进程后需要重新登录。

5. **Windows 专有限定。** windeployqt、MinGW 运行库、控制台代码页三处处理
   目前都是 `Q_OS_WIN` 分支，移植到别的平台需要补相应实现。

6. **三个已修但值得记住的坑**：
    - `QString::arg` 不吃 printf 的 `%-5s` 宽度语法（`-` 不是数字，整段会被当普通
      文本原样输出），要左对齐得用 `leftJustified(5)`。
    - Qt 6 里 `qPrintable(s)` == `s.toLocal8Bit()`，中文 Windows 上产出 **GBK** 字节；
      若已 `SetConsoleOutputCP(CP_UTF8)` 必乱码 —— 统一用 `toUtf8()`，
      且别写 `s.toUtf8().constData()`（临时对象已析构 = 悬垂指针）。
    - QML 里 `height: visible ? implicitHeight : 0` 会造成绑定循环
      （`Text` 的 `implicitHeight` 依赖 `width`，反过来又依赖 height）。
      `Column` 本来就跳过 `visible: false` 的子项，不需要这行。
