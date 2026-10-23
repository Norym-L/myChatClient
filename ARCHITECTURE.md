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
  ⚠ 确认后服务端即删除 → **本地 SQLite 才是历史来源**（`ChatStore`，见 §5.6）。
  演示数据只在 `MYCHAT_DEMO=1` 时注入（用于版式自检）
- ✅ **本地聊天记录**（SQLite）：按 uid 分库、收发先落库再 ACK、未读/已读/发送状态持久化、
  `server_id` 唯一索引兜住 PEL 重投、重启后会话与历史自动回填
- ✅ **自动登录 / 多账号卡片**：登录成功后把凭据写进 `session.ini` 的 `[Accounts]`；
  登录页顶部一排账号卡（左右滑动、单击选中、双击免密进入）。单账号时启动直接进，
  多账号时停在选卡页。token 失效只清凭据、卡片保留
  （自检：`qtClientAccountPickerTest`）
- ✅ **删除 / 清空聊天记录**（只影响本机）：会话头「…」→ 选择消息（批量删）/ 清空会话，
  两个操作都有确认弹窗；服务端没有删除接口，对方不受影响
- ✅ **群聊**（服务端 2026-10-23 起，基线 `df647ad` + `7338a5c`）：创建 1033 / 拉人 1035 /
  踢人 1037 / 解散 1039 / 群成员 1041 / 群资料获取 1043 / 群资料修改 1045
  （`GroupController`）；群消息复用 1013（`group:true`）/ 1024（`group` + `group_id`）/
  1025。群会话键是 `g<gid>`，群名 / 公告由 1043 现拉并缓存在本机 `groups` 表
  （服务端没有群列表 / 退群接口，见 §5.7）
- ✅ **群操作系统消息**：服务端无独立类型，只把 `from_uid` 置为 `"1"`；建群 / 拉人 /
  踢人 / 解散四类正文在群会话里以居中灰字显示，不再当用户气泡解析昵称。
  `ChatController::groupSystemMessage(gid, body)` 通知页面强制刷新成员列表；
  已删除的群会话用 `m_removed_gids` 挡住晚到的解散系统消息
- ✅ 十七套无 GUI 的命令行自检程序（复用同一批控制器，验证的是真实链路）

### 尚未实现

- **服务端历史**：`XACKDEL` 确认即删，跨设备翻不到旧消息（只有本机 SQLite 里有）
- **群列表 / 主动退群**（服务端未实现）：
  客户端只能记住"我建的群"和"收到过消息的群"，群名丢了就显示「群 &lt;gid&gt;」
- **群资料变更没有推送**：1045 成功后其他成员不会收到通知，要自己再拉 1043 才看得到；
  1043 本身也不校验成员身份（知道 gid 就能读）
- **入群 / 被踢通知**没有独立推送：只有一条 `from_uid=1` 的系统消息，
  客户端收到后靠强刷 1041 更新成员列表（`df647ad` 起）

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
│   ├── ChatController.h/.cpp          会话 / 消息（单聊 + 群会话，群键 g&lt;gid&gt;）
│   ├── GroupController.h/.cpp         群管理（创建 / 拉人 / 踢人 / 解散 / 成员 / 资料 1043-1046）
│   │
│   └── core/                   协议 / 网络 / 会话 / 日志（不依赖外部）
│       ├── protocol.h/.cpp        帧格式、MessageId、ReqId→路径、ErrorCode、parseUid
│       ├── HttpClient.h/.cpp      QNetworkAccessManager 单例
│       ├── ChatSocket.h/.cpp      QTcpSocket 单例 + 帧组包/解包
│       ├── ChatStore.h/.cpp       本地聊天记录（SQLite，按 uid 分库）
│       ├── SessionManager.h/.cpp  登录态（uid / name / token）
│       └── logger.h/.cpp          mychat::log，级别过滤与统一格式
│
├── qml/                       QML 页面
│   ├── main.qml                窗口壳 + 路由容器
│   ├── pages/                  LoginPage / RegisterPage / ResetPasswordPage / ChatPage
│   │   └── chat/               ChatPage 拆出来的三个分区 + 消息列表
│   │       ├── ConversationPane.qml  会话列表（244px 左栏）
│   │       ├── ChatPane.qml          右栏：会话头 / 输入区 / 选择模式 / 消息操作
│   │       ├── MessageListView.qml   气泡 / 时间标签 / 滚动条 / 翻页（Flickable + Column）
│   │       ├── ContactsPane.qml      联系人：好友列表 + 搜索用户
│   │       └── ApplyPane.qml         好友申请列表
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
│   ├── contactflow.cpp          好友链路 → qtClientContactTest
│   ├── profileedit.cpp          好友备注 + 编辑资料（1029-1032）→ qtClientProfileEditTest
│   └── groupflow.cpp            群聊（1033-1042 + 群消息 + 系统消息）→ qtClientGroupTest
│   └── groupinfo.cpp            群资料（1043-1046 获取/修改）→ qtClientGroupInfoTest
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
- `sendJsonWithId(message_id, body)` —— 组帧发送，**自动注入客户端请求编号 `msgid`**
  （形如 `r-12`，连接内单调递增；用 `r-` 是为了和业务 `msg_id` 的 `c-` 区分），返回该编号；空串表示没发出去
- `dispatchBuffer()` —— 循环解帧（粘包 / 半包都在这里处理）
- `handleFrame()` —— 按编号分发；已知回包全部有处理，服务端推送 1009 / 1012 / 1022 也在其中，
  真正未知的编号打 WARN 但不断开连接

连接超时、socket 错误、对端断开分别发出 `connectFailed` / `linkError` 信号。

**回包超时是统一的**：每发一个请求就 `expectReply(请求号, 回包号, msgid, 超时回调)` 登记一笔，
回包到了由 `handleFrame` 最前面的 `finishReply()` 销账，到期没回就按 `RequestTimeout`
（客户端本地码 5，文案「网络连接超时，请稍后重试」）回调一次，**并额外发出
`requestTimedOut(msgid, 请求号)`**。

**销账优先按 `msgid` 精确匹配**（服务端 2026-10-22 起在所有响应里原样回显），
回包没带编号时才退回"同型号 FIFO"。这样同类请求并发在飞也不会互相销账；
而超时时能拿到具体编号，就能判断"是这一条被服务端丢了" —— 服务端队列满
（容量 10）时 `try_send` 直接丢帧，既不回包也不回错误码，没有 `msgid` 就只能
笼统报"网络超时"。

这么做的原因不是洁癖 —— 服务端确实存在不回包的分支（队列满、以及拿一条不属于
自己的申请去发 1010）。TCP 层没有任何"一定有回包"的保证，缺了这道闸门
的表现是界面永远转圈、后续请求全被"上一个还没回包"挡掉，而且完全静默。

因此新增消息时 **必须**在发送处取 `sendJsonWithId()` 的返回值并交给 `expectReply()`；
`tests/sockettimeout.cpp` 用一个只收不发的哑服务器把"超时必须回调"钉住，
`tests/msgiddrop.cpp` 另外钉住"超时必须报出正确的 msgid"与"回包不带编号时的 FIFO 兜底"。

### 4.4 SessionManager

内存里保存**当前会话**（`uid` / `name` / `token` / `host` / `port`），
提供 `label()` 生成 `"UID 666672 · agentnjqi"` 这样的显示串。

**落盘的是账号列表而不是单个会话**：`QStandardPaths::AppConfigLocation/session.ini`
（Windows 上是 `%APPDATA%/LocalLow/MyChat/MyChat/session.ini`）：

```ini
[Session]
lastUid=1001                  ; 登录页默认选中它
[Accounts]
1001\token=...                ; 每个账号一份凭据
1001\host=127.0.0.1
1001\port=8091
1001\lastLoginMs=...
```

依据：服务端 token 是**不带 TTL** 的 `redisSet(USER_TOKEN_PREFIX + uid)`，一直有效到
下一次 `/user_login` 覆盖；`1001` 只校验 `{uid, token}`；而 Redis 键**按 uid 分开**，
所以同一台机器可以同时记住多个账号、各自免密进入（登录页那排卡片就是它）。

生命周期：
| 时机 | 动作 |
|---|---|
| 聊天服登录成功 | `save()` 写入/更新该账号 + `lastUid`，清掉 `skipAuto` |
| 启动 | 上次主动退出 → 停在登录页；单账号默认自动进；**多账号 → 停在登录页选卡** |
| 点卡片 | 第一次选中，第二次（或双击）`loginWithAccount(uid)` 免密进入 |
| `1012` / `1011` | `dropToken(uid)`：删凭据、**卡片保留**（改输密码） |
| 连接失败（服务端没起） | 只提示，**不动**凭据 |
| 用户退出登录 | **保留凭据**（点卡片还能进）+ 记 `skipAuto`（下次启动不自动登） |
| 卡片「移除」 | `forgetAccount(uid)`：连凭据一起删，这才是"忘记这个号" |
| 旧版单账号记录 | `migrateLegacy()` 自动搬进 `[Accounts]`，用一次不重登 |

> 退出登录的语义：**断开连接 + 下次启动停在登录页**，但账号还记着 ——
> 回来点一下卡片就进去，不用再输密码。要彻底忘掉用卡片上的「移除」。`MYCHAT_SESSION_FILE=<path>` 可改存储位置（自检程序靠它隔离）。

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
收 1022 推送 → 1023 拉取 → 1024 落库 → 1025 确认。历史在本地 SQLite（服务端确认即删除）。
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
`activePeerUid` / `activePeerName` / `activePeerIsDemo` / `activeIsGroup` / `activeGroupId` /
`tip` / `tipIsError`。

**群会话的约定**（2026-10-23 起）：

1. 会话键是 **`g<gid>`**：群号与用户 uid 都是数字、来自两张表，可能撞号；
   界面上显示的仍是纯群号（`conversations[].gid`）。
2. 群会话**永远不在** `friend_uids` 里 —— `syncFriends()` 必须显式放行
   `kind == "group"`，否则刚建好、还没说话的群会被"非好友空壳"规则删掉。
3. 群消息的 `to_uid` 是**群号**，请求体要带 `"group": true`（JSON 布尔）；
   服务端会把它也写进**发送者自己**的离线流 → 拉到 `from_uid == 自己` 的群消息
   要**丢弃回显但仍 ACK**（见 `m_self_uid`）。
4. `1024` 的 `msgs[].group_id` 决定归属；没有 `group_id` 的历史群消息确认掉、不上屏。
5. 群名 / 公告：打开成员面板时用 **1043** 现拉（`GroupController.loadGroupInfo`），
   成功后缓存在本机 `ChatStore::groups`（schema v2）；本地没有缓存时显示「群 &lt;gid&gt;」。
6. **群操作系统消息**：服务端无独立类型，只在群会话里把 `from_uid == "1"` 认成系统消息
   （建群 / 拉人 / 踢人 / 解散）；消息项带 `system: true`，渲染成居中灰字，
   `senderName` 留空（否则会显示成用户 "1"）。
7. 自己删掉群会话后（解散 / 被踢），晚到的系统消息不能复活会话：
   `removeGroupConversation()` 把 gid 记进内存集合 `m_removed_gids`，
   `onChatMessages()` 在 `ensureGroupConversation()` 之前挡掉；`registerGroup()` 时清除。
8. **群资料修改（1045）**：只允许群主 / 管理员；可选字段缺省或 `null` = 保留、
   空串 = 清空（头像 / 公告），群名空串非法；成功后服务端**不推 1022**，
   客户端必须再发 1043 刷新，并通过 `groupInfoSaved(gid, name)` 把新群名写回会话列表。
   1046 在数据库异常（-10 / -11）时**没有 error 字段**，按失败处理。

### 5.7 GroupController

群管理链路的唯一入口（1033–1046）。**群消息不在这里**（复用单聊的 1013/1024，
归 ChatController）。

| 方法                                  | 用途                                             |
|---------------------------------------|--------------------------------------------------|
| `createGroup(name, memberUids)`       | 1033 建群 → 成功后自动 1035 拉人 → 1041 拉成员   |
| `addMembers(gid, uids)`               | 1035，成功后自动重拉成员                         |
| `kickMember(gid, uid)`                | 1037，成功后自动重拉成员                         |
| `dismissGroup(gid)`                   | 1039（仅群主），成功后页面删本地会话             |
| `loadMembers(gid, force)` / `clearMembers()` | 1041；同群不重复请求（`m_members_loading` 挡住并发），`force` 用于收到系统消息后强刷 |
| `loadGroupInfo(gid, force)`           | 1043 拉群资料（群名 / 群主 / 公告 / 人数 / 上限 / 创建时间）；同群命中缓存不重复请求 |
| `changeGroupInfo(gid, name, announcement)` | 1045 改群名 / 公告（头像暂不开放，服务端没有上传通道）；成功后强制重拉 1043 |

属性：`memberList`（`uid` / `nickG` / `role` / `roleText` / `joinAtText` /
`displayName` / `isOwner` / `isMe` / **`canKick`**）/ `memberCount` / `gid` /
`myRole` / `canManage` / `isOwner` / `groupInfo`（`loaded` / `name` / `avatar` /
`ownerUid` / `announcement` / `maxMembers` / `memberCount` / `createAt`）/
`infoLoading` / `busy` / `tip`。

几条要点：

1. **回包不带群 id**（除 1034 的 `group_id`）→ 同一时刻只允许一条群管理请求在飞
   （`ChatSocket::m_group_in_flight`），回包靠请求侧记下的上下文补全信号。
2. `canKick` 按服务端规则复刻：群主可踢任何非群主；管理员只能踢普通成员；
   都不能踢自己。真正的裁决仍在存储过程。
3. `myRole` 是**从成员列表里认出自己**算出来的（服务端不单独下发）；
   自己的 uid 来自 `loginSucceeded`，不能用 `SessionManager`（自检里没人 update 它）。
4. 建群后把 `m_gid` 记成新群：否则 `onGroupMembersAdded` 里的 `m_gid == gid`
   不成立，创建完群成员面板会一片空白。
5. 建群存储过程已修正（`df647ad` 起 `CALL create_group(...)`），建群 →
   拉人 → 成员面板可以一次走通；被踢 / 解散时服务端会先给所有成员写系统消息再动 Redis。
   客户端在 `ChatPage.qml` 用 `onGroupSystemMessage(gid, body)` 调
   `GroupController.loadMembers(gid, true)` 强刷成员面板。
6. `groupInfo` 的 `memberCount` 来自 `groups.member_count` 冗余列，和 `memberList` 长度
   冲突时以成员列表为准；`maxMb` / `mbCnt` / `createAt` 在服务端都是字符串，
   `createAt` 是毫秒时间戳。
7. 1043 **不校验成员身份**（知道 gid 就能读），且每次直查 MySQL、没有 Redis 缓存；
   1045 成功后也不推送，所以面板打开、保存后各要拉一次 1043。

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
11. **兜底拉取**：登录后每 **30s** 主动发一次 1023（即使一条 1022 都没有），断线停止、
    重登再启动。服务端推送链路曾出现"静默丢 1022"的竞态（2026-10-23 修掉的旧连接
    误删新会话映射），这层保底让最坏情况也只晚一个周期，不会表现成"能发不能收、重登才恢复"。
    自检用 `MYCHAT_FALLBACK_PULL_MS` 缩短周期（`tests/fallbackpull.cpp`）。

**发送链路的关键约定**：

1. **`msg_id` 由本类生成**（`c-<本地气泡 id>`），服务端原样回显在 1014 里 ——
   回执靠它精确对回气泡，不依赖发送顺序。别在别处自己拼 msg_id 发 1013。
2. `ChatSocket` 仍然**串行发送**：它的 `m_pending` 超时表按回包型号 FIFO 认领，
   并发时先回的那条会销掉别人的账。
3. `error == 0` 只代表"已写入对方离线消息流"，**不是**"对方已收到"，文案别写"已送达"。
4. 断线时 `ChatSocket` 直接清空队列（不补报超时），所以 `ChatController` 监听
   `linkError` 把 `m_sending` 里的气泡统一标成 `failed`。

**本地记录的约定**（`ChatStore`，SQLite）：

| 项 | 做法 |
|----|------|
| 库位置 | `<exe 目录>/data/chat_<uid>.db`（便携式；`MYCHAT_DATA_DIR` 可覆盖，自检用） |
| 分库 | **按账号分**：换号登录历史天然隔离，表里不用再挂 owner_uid |
| 落盘顺序 | 收到 1024 → **先写库 → 再 ACK**。反了的话进程崩在中间就两头都没了 |
| 去重 | 收到：`messages.server_id` 上的**部分唯一索引**（`WHERE server_id <> ''`）；自己发的没有编号，不参与 |
| 状态 | `sending` 读回来一律降级成 `failed`（进程被杀时在飞的消息不可能再有回执） |
| 会话 | `conversations` 表存 last_text / last_time / unread / updated_ms，列表与红点重启后都在 |
| 加载 | 登录后回填会话；打开会话时懒加载最近 **200** 条（第一版不做翻页） |
| 失败 | 库打不开（缺 `sqldrivers/qsqlite.dll`、目录没权限）→ 只提示一句，聊天照常，退化成纯内存 |
| 删除 | `deleteMessages(ids)` / `clearConversation(uid)`：事务内批量 DELETE；删完按库里最后一条**重算摘要**；清空会话连摘要行一起删（不回写空行） |

⚠ **别给字符串列随便加 `NOT NULL`**：Qt 把空 `QString` 绑定成 NULL，
`INSERT OR IGNORE` 遇到 NOT NULL 违约会**静默丢整行**——表现是"自己发的消息一条都没存进去"。
（这个坑踩了两次：`server_id`、`from_uid`。）

⚠ **`syncFriends()` 不能把非好友会话删掉**：服务端不校验好友关系，陌生人也能发消息；
之前按"好友列表投影"重建列表，重启后本地有记录、界面上那个会话却消失了。
现在规则是：好友更新资料、**非好友只在其有聊天记录时保留**、删掉的好友留下的空壳才丢弃。

⚠ **`syncFriends()` 里的信号重入陷阱**：`emit conversationsChanged()` 之后 QML 会 **同步**
自动打开第一个会话（`ChatPage` 的 handler），所以"原来有没有活动会话"必须在 emit **之前**
记下来。之前写成 emit 之后再判断，结果订阅方刚打开就被 `closeConversation()` 关掉 ——
现象是列表里有好友、右侧停在「未选择会话」，日志完全正常。

⚠ **QML 绑定的坑**：`PresenceController.hasState(uid)` 这类 **函数调用**在 QML 眼里是黑盒，
属性变了也不会重算绑定（曾表现为日志里明明回了 offline、界面一直停在"状态未知"）。
需要依赖时把 `PresenceController.online` 当 **参数**传进函数（见 `ChatPage.peerStateText`）。

⚠ **选择模式不能"收到 activeConversationChanged 就退出"**：好友列表同步
（`syncFriends()`）也会 emit 这个信号（名字 / demo 标记可能变了），
早先那样写会让刚进的选择模式被那次同步立刻退掉（截图里操作条根本出不来）。
判断条件是"`activePeerUid` 真的换了人"。

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
| `chat/ConversationPane` | `ZTextField`、`ZIconButton` + 自建 `ConversationRow` |
| `chat/ChatPane`     | `ZButton`、`ZIconButton`、`ZTag`、`ZTextField`、`ZChatInput`、`ZDialog`、`ChatScrollBar` + 自建 `MessageListView` / `ChatBubble` |
| `chat/MessageListView` | `ChatBubble` + 自建 `ChatScrollBar` |
| `chat/ContactsPane` | `ZSectionTitle`、`ZTextField`、`ZButton`、`ZTag` + 自建 `ZAvatar` / `ConversationRow` |
| `chat/ApplyPane`    | `ZSectionTitle`、`ZButton`、`ZTag` + 自建 `ZAvatar` |

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

1. **认证 + 好友链路 + 好友列表 + 收发消息 + 本地记录 + 群聊已完成。**
   发 1013/1014（`msg_id` 回显配对）、收 1022 → 1023 → 1025（`XACKDEL`）。
   ⚠ 服务端**确认即删除**，所以历史只在**本机 SQLite** 里（`ChatStore`）——
   换台机器 / 换账号看不到；卸载或删库就没了。后续可参考 `docs/ServerDocs/api-chat.md`。
   群聊的已知缺口全在服务端：没有群列表 / 退群 / 资料变更推送；
   建群存储过程已在 `df647ad` 修正，系统消息（`from_uid=1`）已在本机渲染，
   群资料获取 / 修改（1043–1046）已在 `7338a5c` 接入。

2. **`ChatSocket::handleFrame` 的 default 分支是预留扩展点。**
   目前处理了登录 / 搜索 / 发申请 / 申请列表 / 好友列表 / 心跳 / 查在线 / 发消息 /
   新消息推送 / 拉取 / 确认 / 群管理（1034 / 1036 / 1038 / 1040 / 1042 / 1044 / 1046），
   未知编号打 WARN 不断连。

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
