# 无头自检用法

所有开关都是环境变量，对 **已构建好的 qtClient.exe** 生效（在 `build/<preset>/` 里）。

| 环境变量                            | 作用                                                                                                                                         | 退出码                                  |
|-------------------------------------|----------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------|
| `MYCHAT_SMOKE=<输出 png 路径>`      | 渲染一帧后抓图落盘并退出                                                                                                                     | 0 成功；3 根对象不是 Window；4 保存失败 |
| `MYCHAT_SMOKE_DELAY=<ms>`           | 覆盖抓图前的等待时长（默认 1.5s，带 `AUTOLOGIN` 时 4s）。验心跳这类"要等一会儿才发生"的行为时，要跑到 25s+                                   | —                                       |
| `MYCHAT_AUTOLOGIN=<uid>:<password>` | 启动后自动发起一次登录                                                                                                                       | 登录成功则切到聊天页                    |
| `MYCHAT_PAGE=<页名>`                | 指定起始页：`Login`（默认）/ `Register` / `Reset` / `Chat`，大小写不敏感                                                                     | —                                       |
| `MYCHAT_SEARCH=<关键字>`            | 进聊天页后自动发起一次用户搜索（切到「联系人」分区）                                                                                         | —                                       |
| `MYCHAT_OPENDLG=<模式>`             | 进聊天页后自动打开「用户资料」弹窗（`other` / `self` / `selfedit` / `sendfriend[:uid]` / `applylist` / `applyreceived` / `applysent`），见下 | —                                       |
| `QT_ASSUME_STDERR_HAS_CONSOLE=1`    | 让 Qt 日志进 stderr（**必设**，否则重定向到文件什么也看不到）                                                                                | —                                       |
| `QT_QPA_PLATFORM=offscreen`         | 无显示环境 / 无人值守时用（需要 `platforms/qoffscreen.dll`）                                                                                 | —                                       |

## 抓一张登录页的图

```bash
cd qtClient/build/mingw-debug
export QT_ASSUME_STDERR_HAS_CONSOLE=1
MYCHAT_SMOKE=C:/dev/MyChat/qtClient/build/login.png ./qtClient.exe
```

## 抓注册页 / 重置密码页

不加 `MYCHAT_PAGE` 的话永远只能抓到登录页：

```bash
MYCHAT_PAGE=Register MYCHAT_SMOKE=C:/dev/MyChat/qtClient/build/register.png ./qtClient.exe
MYCHAT_PAGE=Reset    MYCHAT_SMOKE=C:/dev/MyChat/qtClient/build/reset.png    ./qtClient.exe
```

页名写错会回落登录页并在 stderr 打一条 WARN，不会静默起一个空白窗口。

## 抓「登录成功后的聊天页」

两个变量一起给，`MYCHAT_SMOKE` 会自动把等待时间从 1.5s 放宽到 4s，
等「HTTP 登录 → 连接 ChatServer → TCP 握手 → 切页」走完再抓：

```bash
MYCHAT_AUTOLOGIN="666672:Test@12345" \
MYCHAT_SMOKE=C:/dev/MyChat/qtClient/build/chat.png ./qtClient.exe
```

## 协议探针（不依赖客户端）

服务端是"会动的那一端"时，先用裸帧把「服务端到底怎么回」钉死，再回头改客户端 ——
比在客户端里加日志猜要快得多。两个脚本只用 Python 标准库。

```bash
PY=C:/Users/Norym/.workbuddy/binaries/python/versions/3.13.12/python.exe

# 心跳帧的两种 body 形态 + 「查在线」回包的真实 message_id（后面的 uid 可省）
"$PY" tools/probe-tcp.py 8090 666673

# 在线 key 的 TTL：心跳有没有真的在续期
"$PY" tools/probe-redis.py uip_666672
```

判读要点：

- 未登录也能发心跳 —— 服务端会回 `error=1011` 且 **不关连接**，所以不用登录就能验帧路径。
- `probe-tcp.py` 打印的 `message_id` 是 **回包自己带的编号**。踩过：服务端
  `checkIsUserOnlineHandler` 里 `send(...)` 误用 `MESSAGE_ADD_FRIEND_RSP`（1006），
  而客户端在等 1019 —— 这种错帧只看客户端日志很难定位。
- `probe-redis.py` 的推理：客户端只跑 15 秒、退出后立刻读 TTL， **≈25 才说明 t=10s 的心跳续上了**（登录那次写的此刻只该剩
  ≈15）。

## 抓「用户资料」弹窗

弹窗是 `Popup`， **离屏抓图默认抓不到它** —— 得让它自己弹出来，所以才有了
`MYCHAT_OPENDLG`：

```bash
# 他人资料卡（假数据，uid 666673）
MYCHAT_PAGE=Chat MYCHAT_OPENDLG=other \
MYCHAT_SMOKE=C:/dev/MyChat/qtClient/build/dlg-other.png ./qtClient.exe

# 自己的资料卡（只读）／编辑态
MYCHAT_PAGE=Chat MYCHAT_OPENDLG=self     MYCHAT_SMOKE=... ./qtClient.exe
MYCHAT_PAGE=Chat MYCHAT_OPENDLG=selfedit MYCHAT_SMOKE=... ./qtClient.exe

# 联调（真实账号）：本人的昵称 / uid 从会话里取
MYCHAT_AUTOLOGIN="666672:Test@12345" MYCHAT_OPENDLG=self MYCHAT_SMOKE=... ./qtClient.exe
```

`MYCHAT_OPENDLG=sendfriend[:<uid>]` 会在打开他人资料卡后替用户点一次「加好友」，
用来验证 `弹窗 → 页面 → ContactController → 服务端 → 提示回流` 整条链路。
不给 uid 时默认用 `999999999`（格式合法但一定不存在）——服务端会回 `1011`，
不会在库里留下真实的好友申请； **要验证"发成功"那条路**就写成
`sendfriend:666673`（换成一个真实存在的 uid，注意会真的插一条申请记录）。
日志里能直接看到 `发送 AddFriendReq` / `收到 AddFriendRsp`，
帧里能核对 `verify_info` 与 `remark` 是否真的带上去了。

回包之后它还会 **再点一次**「加好友」（1500ms 后）。 **发送成功**时弹窗应已切成
「申请详情」、主按钮是禁用的「申请已发送」，所以日志里 `发送 AddFriendReq`
**只应出现 1 条** —— 这就是"防止重复发送"的回归点。
（发送失败时会保留「加好友」让人重试，那种情况下出现第 2 条是 **对的**。）

`MYCHAT_OPENDLG=applylist` 只切到「好友申请」分区（不弹资料卡），
`applyreceived` / `applysent` 则打开列表里第一条「收到」/「已发出」申请的资料卡
—— 这时弹窗是**「申请详情」形态**：只展示那条申请（方向 / 状态 / 验证信息），
底栏不再给「加好友」，而是禁用态的「申请已发送」/「待处理」。

后两个是 **回归用**：

- 服务端这条查询是「我发出的 + 我收到的」混在一起
  （`WHERE applicant_uid = ? OR receiver_uid = ?`），name 却是对方的 ——
  若 UI 拿 `fromUid` 当"对方"，我发出的那条会弹出「我的资料」编辑态（曾经就是这样）。
- 同理，那种情况下若还留着「加好友」按钮，点下去必然是重复申请。

> **中文字体**：离屏平台用的是「从 `<exe目录>/lib/fonts/` 读字体」那套通用字体库，
> 目录不存在时会打 `QFontDatabase: Cannot find font directory`， **中文全渲染成方框**
> （布局仍是准的，只是字看不见）。修法：把字体拷进去，例如
>
`mkdir -p build/mingw-debug/lib/fonts && cp /c/Windows/Fonts/{msyh.ttc,simhei.ttf,consola.ttf} build/mingw-debug/lib/fonts/`
> （`build/` 不入 git，随构建目录存在即可）。想再放大一档看字，加 `QT_SCALE_FACTOR=2`。

## 为什么不用 `qml.exe` 加载 QML 来截图

早期版本用 `qml.exe -I third_party tools/smoke.qml` 直接实例化 `qml/main.qml`。
现在 `qml/main.qml` 会 `import MyChat.Core 1.0`，而 `LoginController` 是 **C++ 注册的
单例**（`qmlRegisterSingletonInstance`）——纯 QML 运行时里没有它，加载阶段就报
`module "MyChat.Core" is not installed`。

而且就算能加载，那条路走的也是 **文件路径**，跟 exe 真实走的 `qrc:/qml` 是两套
解析机制。放到 exe 内部抓图，验证的才是真正会出问题的那条路径。

## 注意

**判断 GUI exe 是否真的起来，绝不能只看进程是否存活** —— QML 加载失败时 Qt 会弹
错误对话框，进程照样活着。必须设 `QT_ASSUME_STDERR_HAS_CONSOLE=1` 然后读 stderr
有没有 `failed to load component` / `is not installed` 之类。
