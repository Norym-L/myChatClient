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
- ✅ 四套无 GUI 的命令行自检程序（复用同一批控制器，验证的是真实链路）

### 尚未实现

- 会话列表、消息气泡、聊天输入框 —— **依赖服务端补齐**（好友列表接口是空函数，文本聊天消息无实现）
- 好友列表（服务端未实现）、认证 / 拒绝好友申请（1009）
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
│   ├── ContactController.h/.cpp       联系人 / 好友链路（搜索 / 发申请 / 申请列表）
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
│   └── components/ZAvatar.qml  首字母头像（自建，ZmdUI 目前没有头像组件）
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

- `HEAD_ID_LEN = 2`、`HEAD_DATA_LEN = 2`、`MAX_BODY_LEN = 2048`
- body 超上限按非法帧处理，断开会话
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
- `handleFrame()` —— 按编号分发；目前处理 `ChatLoginRsp` / `SearchUserRsp` /
  `AddFriendRsp` / `SyncFriendRequestRsp` 四个回包，其余打 WARN 但不断开连接

连接超时、socket 错误、对端断开分别发出 `connectFailed` / `linkError` 信号。

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

联系人 / 好友链路的唯一入口，覆盖服务端 **已实现** 的三条 TCP 消息：

| 方法                                           | 对应消息                                | 说明                                      |
|------------------------------------------------|-----------------------------------------|-------------------------------------------|
| `searchUser(keyword)`                          | `SearchUserReq/Rsp`（1003/1004）        | keyword 可为 uid 或用户名，服务端两种都认 |
| `sendFriendRequest(toUid, verifyInfo, remark)` | `AddFriendReq/Rsp`（1005/1006）         | toUid 按字符串传                          |
| `refreshApplyList()`                           | `SyncFriendRequestReq/Rsp`（1007/1008） | 拉取「别人发给我的」申请列表              |
| `clearSearchResult()`                          | ——（本地）                              | 清空搜索态                                |

属性：`tip` / `tipIsError` / `busy`、搜索结果 `hasSearchResult` / `searchUid` /
`searchName` / `searchDesc` / `searchSex`、申请列表 `applyList`（`QVariantList`，
每项含 `fromUid` / `name` / `verifyInfo` / `state`，供 Repeater 直接用）。

信号：`searchFinished(found, error)`（未找到通常是 1014）、
`friendRequestSent(ok, error)`、`validationFailed(field)`。

⚠ 服务端的好友列表（`getUserApplyListAndFriendList`）是 **空函数**，
认证好友（1009）与文本聊天（1012）也没实现 —— 会话列表因此拿不到任何真实数据。

### 5.6 路由

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

| 页面                | 组件                                                                                               |
|---------------------|----------------------------------------------------------------------------------------------------|
| `LoginPage`         | `ZPanel`、`ZTextField`、`ZButton`、`ZGridBackground`                                               |
| `RegisterPage`      | 同上                                                                                               |
| `ResetPasswordPage` | 同上                                                                                               |
| `ChatPage`          | `ZSideBar`、`ZPanel`、`ZTextField`、`ZButton`、`ZSectionTitle`、`ZGridBackground` + 自建 `ZAvatar` |

用到的 Theme 令牌：`bg` `surface` `ink` `inkSoft` `muted` `accent` `danger` `lineSoft`
`gridLine` `gridLineMajor` `fontBody` `fontStrong` `fontMonoMd` `fontMonoSm` `spacingSm`
`spacingMd` `spacingLg` `Variant.*`。

**业务 QML 里不写魔法数字和硬编码颜色**，一律取 `Theme`。

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

1. **认证 + 好友链路已完成；聊天业务未做。** 会话列表、消息气泡、聊天输入框都还没有。
   其中一部分卡在服务端：好友列表接口是空函数、文本聊天消息（1013~1015）无实现。
   后续可参考 `docs/ServerDocs/api-http.md`、`api-tcp.md` 与根目录的 `接口文档.md`。

2. **`ChatSocket::handleFrame` 的 default 分支是预留扩展点。**
   目前已处理 4 个回包（登录 / 搜索 / 发申请 / 申请列表），未知编号打 WARN 不断连。
   后续要接入的编号：`NotifyAddFriendReq`(1009，服务端主动推送，客户端暂未处理)、
   `AuthFriend*`(1010/1011，服务端也未实现)、`TextChat*`(1013~1015)。

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
