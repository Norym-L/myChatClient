# 无头自检用法

所有开关都是环境变量，对 **已构建好的 qtClient.exe** 生效（在 `build/<preset>/` 里）。

| 环境变量                            | 作用                                                            | 退出码                                  |
|-------------------------------------|-----------------------------------------------------------------|-----------------------------------------|
| `MYCHAT_SMOKE=<输出 png 路径>`      | 渲染一帧后抓图落盘并退出                                        | 0 成功；3 根对象不是 Window；4 保存失败 |
| `MYCHAT_AUTOLOGIN=<uid>:<password>` | 启动后自动发起一次登录                                          | 登录成功则切到聊天页                    |
| `MYCHAT_PAGE=<页名>`                | 指定起始页：`Login`（默认）/ `Register` / `Reset`，大小写不敏感 | —                                       |
| `QT_ASSUME_STDERR_HAS_CONSOLE=1`    | 让 Qt 日志进 stderr（**必设**，否则重定向到文件什么也看不到）   | —                                       |

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
