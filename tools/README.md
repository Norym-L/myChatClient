# 无头自检用法

所有开关都是环境变量，对 **已构建好的 qtClient.exe** 生效（在 `build/<preset>/` 里）。

| 环境变量                            | 作用                                                                                                                                                                     | 退出码                                  |
|-------------------------------------|--------------------------------------------------------------------------------------------------------------------------------------------------------------------------|-----------------------------------------|
| `MYCHAT_SMOKE=<输出 png 路径>`      | 渲染一帧后抓图落盘并退出                                                                                                                                                 | 0 成功；3 根对象不是 Window；4 保存失败 |
| `MYCHAT_SMOKE_DELAY=<ms>`           | 覆盖抓图前的等待时长（默认 1.5s，带 `AUTOLOGIN` 时 4s）。验心跳这类"要等一会儿才发生"的行为时，要跑到 25s+                                                               | —                                       |
| `MYCHAT_AUTOLOGIN=<uid>:<password>` | 启动后自动发起一次登录                                                                                                                                                   | 登录成功则切到聊天页                    |
| `MYCHAT_PAGE=<页名>`                | 指定起始页：`Login`（默认）/ `Register` / `Reset` / `Chat`，大小写不敏感                                                                                                 | —                                       |
| `MYCHAT_SEARCH=<关键字>`            | 进聊天页后自动发起一次用户搜索（切到「联系人」分区）                                                                                                                     | —                                       |
| `MYCHAT_OPENDLG=<模式>`             | 进聊天页后自动打开「用户资料」弹窗或某个视图（`other` / `self` / `selfedit` / `sendfriend[:uid]` / `applylist` / `applyreceived` / `applysent` / `session[:uid]` / `creategroup` / `groupmembers`），见下 | —                                       |
| `MYCHAT_GROUPCREATE=<群名>:<uid,...>` | 进聊天页后自动建群（真实的 1033 → 1035 链路，成员可省略），用来验证群会话 / 群消息                                                                                     | —                                       |
| `MYCHAT_SENDMSG=<文本>`             | 进聊天页后替用户发一条消息（走真实的 输入框 → ChatController → 本地回显 链路），见下                                                                                     | —                                       |
| `MYCHAT_AUTHAPPLY=<accept\|refuse>` | 进聊天页后切到「好友申请」，挑一条「收到 + 待处理」的申请，打开资料卡并替用户点「接受 / 拒绝」，见下                                                                     | —                                       |
| `MYCHAT_THEME=<light\|dark>`        | 启动前把主题存档改掉，等价于用户点左下角的「浅色 / 暗色」，见下                                                                                                          | —                                       |
| `QT_ASSUME_STDERR_HAS_CONSOLE=1`    | 让 Qt 日志进 stderr（**必设**，否则重定向到文件什么也看不到）                                                                                                            | —                                       |
| `QT_QPA_PLATFORM=offscreen`         | 无显示环境 / 无人值守时用（需要 `platforms/qoffscreen.dll`）                                                                                                             | —                                       |

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
比在客户端里加日志猜要快得多。四个脚本只用 Python 标准库。

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

## 抓「会话界面」并验证发消息

会话分区是 **默认分区**（登录后第一眼就是它），所以 `MYCHAT_PAGE=Chat` 就能直接拍到：

```bash
# 纯版式（不带登录：右上角自己的头像会退化成"我"）
MYCHAT_PAGE=Chat MYCHAT_SMOKE=C:/dev/MyChat/qtClient/build/chat-ui.png ./qtClient.exe

# 带登录 + 自动发一条消息：验证「输入框 → 控制器 → 1013 → 1014」
# 产物里不该出现「发送中」/「发送失败」，日志里应有
#   [chat] 消息已写入离线消息流, to_uid = ... msg_id = c-N ...
MYCHAT_AUTOLOGIN="666672:Test@12345" MYCHAT_SENDMSG="这条是刚发的" \
MYCHAT_SMOKE=C:/dev/MyChat/qtClient/build/chat-final.png MYCHAT_SMOKE_DELAY=4500 ./qtClient.exe

# 只切到会话分区 / 打开指定会话
MYCHAT_PAGE=Chat MYCHAT_OPENDLG=session:666674 MYCHAT_SMOKE=... ./qtClient.exe
```

判读要点：

- 发出去的那条气泡：成功时**不挂状态小字**（只失败/发送中才显示）；
  挂「发送失败」说明服务端没回包或回了错误码，去日志里找 `error =`。
- 服务端成功只代表"写进对方离线消息流"，不代表对方收到 —— 别据此判断在线状态。
- 会话列表左下角常驻 `DEMO · 本地演示数据`，同样是在如实标注数据来源。
- 消息多到放不下时应该自动滚到底（消息列表挂在 `contentHeight` 上滚，
  不是在 `messagesChanged` 里滚 —— 那一拍新 delegate 还没布局完，会差一条的高度）。

## 验证好友申请的接受 / 拒绝（1010/1011）与结果推送（1012）

两条方向都要验，而且都需要"另一个在线的人"：

**① 我是接收方，处理别人发来的申请**

```bash
# 客户端登录成 B，收到申请后自动点「接受」（走 资料卡 → 页面 → 控制器 → 1010）
MYCHAT_AUTOLOGIN="666672:Test@12345" MYCHAT_AUTHAPPLY=accept \
MYCHAT_SMOKE=.../friend-accepted.png MYCHAT_SMOKE_DELAY=13000 ./qtClient.exe &

# 另一头用裸连接扮演 A 发申请，并挂着等结果推送
"$PY" tools/probe-push.py 666674 "NewPass@1" 666672 "验证接受" --listen 9
```

B 的日志应有：`收到 NotifyAddFriendReq` → `收到好友申请列表` → `发送 AuthFriendReq`
→ `收到 AuthFriendRsp {"error":0}` → `处理好友申请, ok = true`；弹窗底栏从
「接受 / 拒绝」收成禁用的「已接受」。

**② 我是申请人，别人处理了我发的申请**

```bash
# 客户端登录成 A，自动发一条申请并保持在线
MYCHAT_AUTOLOGIN="666674:NewPass@1" MYCHAT_OPENDLG=sendfriend:666672 \
MYCHAT_SMOKE=.../friend-refused.png MYCHAT_SMOKE_DELAY=15000 ./qtClient.exe &

# 以 B 的身份接受 / 拒绝（会先拉列表找出那条申请）
"$PY" tools/probe-auth.py 666672 Test@12345 refuse --applicant 666674
```

A 的日志应有 `收到 NotifyAuthFriendReq {"add_friend_request_id":"20",...,"state":2}`
→ `对方已处理我的好友申请`，弹窗底栏从「申请已发送」变成「对方已拒绝」。

判读要点：

- **1011 会回显申请编号**（2026-10-17 起，成功失败都带），所以结果按回包自己认领；
  但 **`state` 不回显** —— 接受还是拒绝仍取自请求侧，认领不到时按"未知"处理并回拉列表。
- **1012 的编号字段实际是字符串**（`"add_friend_request_id" : "20"`），
  与 `api-friend.md` §6 写的"数字"不符；客户端用 `parseUid` 两种都吃，不受影响。
- 处理成功后客户端 **不关弹窗**：结果提示要留在底栏，关掉就只看到列表变了一行。
- 自动刷新（推送触发的补拉） **不会覆盖提示** —— 否则"对方接受了你的申请"一秒后
  被"共 N 条好友申请"盖掉。
- `probe-auth.py --dump` 会原样打印 1008 的每一条。核对 **备注**（`nick`）时用它：
  这个字段 **只有发送方视角有值**，接收方那边恒为空，光看界面分不清是"没传"还是"没显示"。

## 抓两套主题（亮 / 暗）

```bash
cd build/mingw-debug
for mode in light dark; do for pg in Login Register Reset Chat; do
  MYCHAT_PAGE=$pg MYCHAT_THEME=$mode MYCHAT_SMOKE=.../theme-$mode-$pg.png \
  MYCHAT_SMOKE_DELAY=1500 QT_QPA_PLATFORM=offscreen ./qtClient.exe
done; done
```

判读要点：

- `MYCHAT_THEME` 写的是 **存档本身**（`Theme/mode`），不是另开一处状态 —— 所以
  "自动化改的" 和 "用户点出来的" 是同一份。验持久化时连续跑两次，第二次 **不给这个变量**，
  界面应该保持上次的选择（实测：跑完 dark 再跑不带变量的登录页，背景仍是 `#181818`）。
- 核验不必靠肉眼：采样两个点就能下结论 —— 底色（浅 `#F2F2F2` / 暗 `#181818`），
  以及左下角高亮块落在哪个按钮上（浅色主题高亮 `x≈20–83`，暗色主题 `x≈92–155`）。
  4 页 × 2 套 = 8 张，全对才算过。

## 验证「回包超时」兜底（不需要服务端）

服务端存在"某个分支直接 return、一句都不回"的实现（实测：拿一条不属于自己的申请
去发 1010，服务端连一句都不回）。这种场景没法靠真实服务端稳定复现，所以自检程序
自己在进程里起一个 **只接受连接、永不发数据**的哑服务器：

```bash
./qtClientSocketTimeoutTest      # 退出码 0 = 五条请求全都自己超时收尾
```

它挨个发 登录 / 搜索 / 发申请 / 拉列表 / 处理申请 / 好友列表，每条都必须报 `error=5`
（客户端本地码 `RequestTimeout`，文案「网络连接超时，请稍后重试」）而不是卡死。
覆盖的是 `ChatSocket` 里那张统一的在飞请求表 —— 以后加新消息，只要发送时调一次
`expectReply()`，回包那边由 `handleFrame` 统一销账，不会再出现"某条链路忘了配超时"。

## 验证「发消息」（1013 / 1014，自带假 ChatServer）

真实服务端的回包信息量太少（`{"error":0}`），"请求体字段对不对、回执能不能对回某条消息"
这些只能靠一个会检查帧的假服务器：

```bash
./qtClientChatMsgTest            # 退出码 0 = 全部通过
```

它钉住五件事：`msg_id` / `to_uid` / `type` / `body` 都是**字符串**；1014 回显的 `msg_id`
能精确认领；串行发送（未回包前不发第二条）；`error=1020` 走失败路径；
服务端只收不答时 5s 后报 `RequestTimeout`。最后顺带验了 1022 推送。

## 验证「收消息」（1022 → 1023 → 1024 → 1025，自带假 ChatServer）

真实服务端只会告诉你"有消息"或"给你这几条"，看不出**什么时候拉、拉多少、确认了什么**：

```bash
./qtClientChatPullTest           # 退出码 0 = 全部通过（32 项断言）
```

它验的是策略：登录后主动拉（count=10）；一条 1022 等满 2s 才拉且只拉 1 条；
11 条连推立刻拉（count 夹到 10）；拉到的消息按 `from_uid` 落进对应会话
（陌生自动建会话、非当前会话记未读）；`ids` 是字符串数组且一次 ≤10；
**PEL 重投的同 id 消息要去重但仍要确认**；回来的条数达到请求的 count 就续拉；
空回包不确认。末尾还会另开一个连接读本地库，确认 11 条消息与未读**真的落盘了**。

## 验证「本地聊天记录」（SQLite，不需要服务端）

服务端确认即删除（`XACKDEL`），本地库是唯一的历史来源，所以这条链路单独测：

```bash
./qtClientChatStoreTest          # 退出码 0 = 全部通过（29 项断言）
```

钉住四件事：重开库还能读回来（真落盘）；同一个 `server_id` 只进一条（PEL 重投兜底）；
自己发的消息（无 `server_id`）不受唯一性限制；进程被杀时还是 `sending` 的消息
读回来一律是 `failed`，不会再永远转圈。另外还测了**删除所选 / 清空会话**：
删几条就少几条、重复删返回 0、`lastMessage` 取到剩下那批的最后一条、清空后消息与会话行都没了。
测试库建在系统临时目录，跑完即删。

## 验证「删除 / 清空聊天记录」（两个入口都是只删本机）

`tools` 里的自动化钩子（都进聊天页并等历史到位后执行）：

```bash
# 会话头「…」菜单 / 清空确认弹窗（弹窗离屏抓不到，必须让它自己弹）
MYCHAT_OPENDLG=chatactions   MYCHAT_SMOKE=.../chatactions.png  ./qtClient.exe
MYCHAT_OPENDLG=chatclear     MYCHAT_SMOKE=.../chatclear.png    ./qtClient.exe

# 选择模式：选中前 N 条（底部出现操作条、气泡高亮）
MYCHAT_OPENDLG=chatselect:2  MYCHAT_SMOKE=.../sel.png          ./qtClient.exe

# 直接执行删除所选 / 清空（跳过点击确认，用来验"界面→控制器→库"整条链）
MYCHAT_OPENDLG=chatdelete
MYCHAT_OPENDLG=chatwipe
```

判读：`chatdelete` 之后 `data/chat_<uid>.db` 里消息少一条、列表摘要按剩下的最后一条重算；
`chatwipe` 之后消息表清空、会话摘要行也没了（界面上好友会话还在、显示"暂无消息"）。

## 验证「好友申请推送」（NotifyAddFriendReq / 1009）

推送只发给 **在线**的接收方，而且和会话绑定 —— 所以不能用 `qtClientContactTest` 当发送方
（它发完就退出；第二段还会登录成接收方，把真客户端的会话顶掉）。正确姿势是 **真客户端当接收方 + 裸连接当发送方**：

```bash
# 终端 1：接收方（666672）保持运行，只把日志落到文件
cd build/mingw-debug
MYCHAT_AUTOLOGIN="666672:Test@12345" QT_QPA_PLATFORM=offscreen QT_ASSUME_STDERR_HAS_CONSOLE=1 \
  ./qtClient.exe > ../recv.log 2>&1 &

# 终端 2：裸连接扮演发送方（HTTP 登录拿 token → 连 ChatServer → 发 AddFriendReq）
"$PY" tools/probe-push.py 666674 "NewPass@1" 666672 "来自裸探针的申请"
```

接收方日志里应该按顺序出现：

```
[chat] 收到 NotifyAddFriendReq {"error":0, "add_friend_request_id":16}
[chat] 收到好友申请推送, request id = 16
[contact] 收到好友申请推送, request id = 16
[chat] 发送 SyncFriendRequestReq {"uid":"666672"}      ← 自动回拉，不用用户点
[chat] 收到好友申请列表, 共 1 条
```

想同时证明 **界面**也动了，就把它做成抓图：接收方带
`MYCHAT_SMOKE=.../push-badge.png MYCHAT_SMOKE_DELAY=9000` 起， **在第 4 秒左右**
跑 `probe-push.py`，抓图时左侧「好友申请」角标应从空变成条数。

> 注意：`probe-push.py` 会在 `friend_request` 表里留一条真实记录（同一对用户重复发
> 是 `UPDATE` 同一行、id 不变）。要重跑干净的用例先删：
> `delete from friend_request where applicant_uid=666674 and receiver_uid=666672;`

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

## 验证群聊（服务端 2026-10-23 起）

服务端**没有群列表接口**，探针也不知道群号 —— 群号来自创建回包，或者从拉取到的
群消息里读。建群存储过程已在服务端 `df647ad` 修正（`CALL create_group(...)`），
现在 `MYCHAT_GROUPCREATE` 能直接建群；下面的"手工造群"仍适合测老数据 / 不依赖建群。

```bash
# ① 造一个群（gid=1），并把成员写进 Redis 成员集合
mysql -h 127.0.0.1 -P 3308 -u root -p chatserver \
  -e "CALL create_group('自检群','',666672,500,'',@gid,@res); SELECT @gid,@res;"
redis-cli -p 6380 -a 123456 SADD chatgroup_1 666672 666673

# ② 以成员 B 的身份发一条群消息
"$PY" tools/probe-group.py 666673 Test@12345 send 1 "来自 B 的群消息"

# ③ 客户端登录成 A，打开群会话（会看到「群 1」自动建出来）
MYCHAT_AUTOLOGIN="666672:Test@12345" MYCHAT_OPENDLG=session:g1 \
MYCHAT_SMOKE=.../group-recv.png MYCHAT_SMOKE_DELAY=4500 ./qtClient.exe

# ④ 客户端发一条群消息，再用 B 确认收到（1013 带 "group":true，to_uid=群号）
MYCHAT_AUTOLOGIN="666672:Test@12345" MYCHAT_OPENDLG=session:g1 \
MYCHAT_SENDMSG="客户端发的群消息" MYCHAT_SMOKE=.../group-send.png ./qtClient.exe
"$PY" tools/probe-group.py 666673 Test@12345 pull
```

群成员面板：`MYCHAT_OPENDLG=groupmembers`（自动打开列表里的第一个群）。
客户端侧的正常路径是 `MYCHAT_GROUPCREATE="群名:uid"`（服务端已修存储过程，可直接用）。

群操作系统消息（建群 / 拉人 / 踢人 / 解散，`from_uid=1`）会在消息流里显示成居中灰字；
用两个账号建群 / 拉人 / 踢人即可触发，或直接往某个 uid 的离线流里塞一条 1024 系统消息
（`probe-group.py` 可扩展一个 `push` 子命令）。

## 注意

**判断 GUI exe 是否真的起来，绝不能只看进程是否存活** —— QML 加载失败时 Qt 会弹
错误对话框，进程照样活着。必须设 `QT_ASSUME_STDERR_HAS_CONSOLE=1` 然后读 stderr
有没有 `failed to load component` / `is not installed` 之类。
