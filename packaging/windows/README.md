# Windows 打包（绿色版 + 安装包）

MyChat 客户端是 Qt Quick 应用：Release 构建出来的 exe 不能单独拷走，必须带上
Qt 的 DLL、QML 模块和平台插件。这里把「构建 → 收依赖 → 压缩 → 做安装包」固定成
一条流程，产物都在仓库根目录的 `dist/`。

## 一条命令

```powershell
# 仓库根目录执行
powershell -ExecutionPolicy Bypass -File packaging\windows\package.ps1
```

首次会自动 `cmake --preset mingw-release` 配置并编译，然后：

```
dist/
├─ MyChat-0.1.0/                 ← 绿色版目录，可直接双击 qtClient.exe
├─ MyChat-0.1.0-win64.zip        ← 绿色版压缩包
└─ MyChat-Setup-0.1.0.exe        ← 安装包（装了 Inno Setup 才会生成）
```

常用参数：

| 参数 | 作用 |
|---|---|
| `-SkipBuild` | 不重新编译，直接拿 `build\mingw-release\qtClient.exe` 打包 |
| `-SkipZip` | 只收目录，不压缩 |
| `-SmokeTest` | 打包后用 offscreen 平台跑一次 `MYCHAT_SMOKE`，验证目录确实自包含 |
| `-Version 0.2.0` | 覆盖从 `CMakeLists.txt` 里读到的版本号 |
| `-ConfigFile <路径>` | 换一份 config.ini（默认用 `assets\config.ini`） |
| `-IsccPath <路径>` | 手动指定 `ISCC.exe`（默认自动找） |

## 安装包工具：Inno Setup 6

安装包由 [Inno Setup](https://jrsoftware.org/isinfo.php) 6.3+ 编译（脚本：
`packaging\windows\MyChat.iss`）。没装的时候脚本只产出绿色版 zip，并提示一句。

```powershell
winget install JRSoftware.InnoSetup
# 装完重开一个终端，再跑：
powershell -ExecutionPolicy Bypass -File packaging\windows\package.ps1 -SkipBuild
```

安装包的行为：

- **默认按当前用户安装**到 `%LOCALAPPDATA%\Programs\MyChat`，不需要管理员权限；
  用户在向导里仍可选「为所有用户安装」（那时装到 Program Files，需要 UAC）。
- 开始菜单快捷方式 + 可选桌面图标；卸载项齐全。
- 卸载**不会删** `<安装目录>\data\`（本地聊天记录 SQLite），重装后记录还在。

## 为什么要按用户安装

聊天记录数据库在 **exe 目录下的 `data\`**（`src/core/ChatStore.cpp`），
会话 / token 在 `%APPDATA%\..\LocalLow\MyChat\MyChat\session.ini`。
装进 `Program Files` 的话普通用户对 `data\` 没有写权限，只能走兜底目录；
装到 `%LOCALAPPDATA%` 才能保持"便携"的语义。

## 分发前要改的东西

1. **服务器地址**：`assets\config.ini` 是打包时复制进安装目录的那一份，默认
   `localhost:20060`（本机调试用）。发给别人前改成对方能访问的 GateServer 地址。
2. **版本号**：`CMakeLists.txt` 的 `project(qtClient VERSION ...)`，或者打包时
   `-Version` 覆盖。
3. **图标**：目前 exe 没有资源图标（工程里没有 `.rc`/`.ico`），安装包用的是默认
   图标。要换的话加一个 `.rc` + `set_target_properties(qtClient PROPERTIES
   WIN32_EXECUTABLE TRUE ...)` 那套，顺带在 `.iss` 里加 `SetupIconFile`。

## 手动做（不用脚本）

```powershell
# 1) Release 构建（构建后 CMake 会自动 windeployqt 到 build\mingw-release）
cmake --preset mingw-release
cmake --build build\mingw-release --target qtClient

# 2) 收一份干净的目录（只拷 exe + config.ini，再让 windeployqt 填依赖）
$stage = "dist\MyChat-0.1.0"
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item build\mingw-release\qtClient.exe $stage
Copy-Item assets\config.ini $stage

# 3) 部署 Qt 运行时 / QML / 插件
C:\dev\Qt\6.11.2\mingw_64\bin\windeployqt.exe --release --no-translations `
  --no-opengl-sw --compiler-runtime --qmldir . --dir $stage "$stage\qtClient.exe"

# 4) 压缩
tar -a -c -f dist\MyChat-0.1.0-win64.zip -C $stage .

# 5) 安装包
& "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" `
  "/DMyAppVersion=0.1.0" packaging\windows\MyChat.iss
```

`--qmldir` 必须给仓库根目录：本工程用的是普通 `.qrc` 而不是 `qt_add_qml_module`，
windeployqt 不会自己找到 QML，漏掉这一项会出现「安装完白屏 / 启动即退出」。

## 自检

`-SmokeTest` 会自动做下面这件事；也可以手动跑，验证绿色版确实自包含
（PATH 里没有 Qt 也能启动）：

```powershell
cd dist\MyChat-0.1.0
$env:QT_QPA_PLATFORM = "offscreen"
$env:QT_QPA_FONTDIR = "$env:WINDIR\Fonts"   # offscreen 不读系统字体，不给会全是方框
$env:QT_ASSUME_STDERR_HAS_CONSOLE = "1"
$env:MYCHAT_PAGE = "Login"
$env:MYCHAT_SMOKE = "$PWD\smoke.png"
.\qtClient.exe
# 退出码 0 且生成 smoke.png 就说明 DLL / QML / 插件都齐了
```

注意 `QT_QPA_FONTDIR` 只在 offscreen 模式下需要；正常双击启动走 windows 平台插件，
用的是系统字体，不受影响。

## 已知限制

- 安装包**没有代码签名**，Windows SmartScreen 会提示"未知发布者"。正式对外发布
  需要买证书，然后给 exe 和 setup.exe 做 signtool 签名。
- 安装包界面目前是英文（Inno Setup 6 不自带简体中文语言文件，`MyChat.iss` 里留了
  引入方法）。
- 目前只打 Windows x64；MinGW 构建不支持 32 位。
