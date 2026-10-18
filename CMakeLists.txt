# ══════════════════════════════════════════════════════════════════════
#  qtClient —— MyChat 客户端（Qt Quick + ZmdUI 组件库）
#
#  本仓库**就是**客户端工程本身，不再有子工程层层。
#  完全自包含：协议 / 网络 / 会话 / 日志都在 ./core 下，不引用任何外部文件。
#
#  目录： core/(协议·网络·会话·日志) src/(入口·控制器) qml/(页面)
#         resources/(qrc) assets/(配置) tests/(自检) tools/(用法)
#         third_party/ZmdUI(vendor 组件库)
#
#  ── 两套工具链，通过 CMakePresets.json 切换（CLion 可直接识别）────────
#    mingw-debug / mingw-release   MinGW 13.1.0 + Qt 6.11.2/mingw_64   ← CLion 首选
#    msvc-debug  / msvc-release    MSVC 14.51  + Qt 6.11.2/msvc2022_64
#
#  ⚠ MSBuild 在本机被安全策略拦截，MSVC 配置必须用 Ninja 生成器（不能选 VS 生成器）。
#
#  命令行（不依赖 CLion，需在仓库根目录执行）：
#    cmake --preset mingw-debug
#    cmake --build --preset mingw-debug
#
#  可选开关（-D<名字>=ON/OFF）：
#    QTCLIENT_CONSOLE               保留控制台窗口（默认 OFF，调试 qDebug 时开）
#    QTCLIENT_STATIC_MINGW_RUNTIME  MinGW 下静态链接 GCC 运行库（默认 ON）
#    QTCLIENT_DEPLOY_TO_BUILD_DIR   Windows 下构建后部署 Qt 运行时（默认 ON）
#    QTCLIENT_WARNINGS              开编译警告（默认 ON）
# ══════════════════════════════════════════════════════════════════════

cmake_minimum_required(VERSION 3.21)

project(qtClient
        VERSION 0.1.0
        DESCRIPTION "MyChat 客户端 QML 前端（ZmdUI）"
        LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ⚠ 必须显式打开 AUTORCC。
# qt_standard_project_setup() 只设置 AUTOMOC / AUTOUIC，**不设置 AUTORCC**。
# 少了这一行，resources/qml.qrc 会被静默忽略 —— 配置成功、编译成功、链接成功、
# 一条警告都没有，但 exe 里没有任何 QML 资源，运行时 QQmlApplicationEngine 报
#   qrc:/qml/main.qml: No such file or directory
# 然后 main() 返回 -1（在 CLion 里就显示为"退出代码 -1"）。
# 这个坑只有在运行时才暴露，且极易被误判成"窗口没弹出来"之类的问题。
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOMOC ON)

# 注意：这里**故意不关** CMAKE_CXX_EXTENSIONS（即保留 gnu++20）。
# Qt 自己就是用 gnu++ 编译的，MinGW 下关掉扩展会引入 __STRICT_ANSI__ 差异，得不偿失。

option(QTCLIENT_CONSOLE "保留控制台窗口（调试 qDebug 时开）" OFF)
option(QTCLIENT_STATIC_MINGW_RUNTIME "MinGW 下静态链接 GCC 运行库" ON)
option(QTCLIENT_DEPLOY_TO_BUILD_DIR "Windows 下构建后部署 Qt 运行时到构建目录" ON)
option(QTCLIENT_WARNINGS "打开编译警告" ON)

# ── Qt 查找 ────────────────────────────────────────────────────────────
# ZmdUI 要求 Qt 6.5+；本机 6.11.2。套件由 CMakePresets 的 CMAKE_PREFIX_PATH 指定。
find_package(Qt6 6.5 REQUIRED COMPONENTS
        Core
        Gui
        Qml
        Quick
        QuickControls2      # QML 里 import QtQuick.Controls.Basic，链接上才保证被部署
        Network             # 服务层引入后需要
)

# 编译器家族与 Qt 套件必须匹配：MinGW 配 msvc 套件（或反之）能编过，运行时必崩。
# 这类错误报出来时往往只看到"链接失败/找不到符号"，所以在这里直接拦住并说清怎么改。
if (MINGW AND QT6_INSTALL_PREFIX MATCHES "msvc")
    message(FATAL_ERROR
            "当前用 MinGW 编译，但找到的 Qt 套件是 MSVC 版：\n  ${QT6_INSTALL_PREFIX}\n"
            "请改用 --preset mingw-debug，或把 CMAKE_PREFIX_PATH 指向 C:/dev/Qt/6.11.2/mingw_64")
endif ()
if (MSVC AND QT6_INSTALL_PREFIX MATCHES "mingw")
    message(FATAL_ERROR
            "当前用 MSVC 编译，但找到的 Qt 套件是 MinGW 版：\n  ${QT6_INSTALL_PREFIX}\n"
            "请改用 --preset msvc-debug，或把 CMAKE_PREFIX_PATH 指向 C:/dev/Qt/6.11.2/msvc2022_64")
endif ()

# ── 引入 third_party/ZmdUI ─────────────────────────────────────────────
set(ZMDUI_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ZmdUI")
if (EXISTS "${ZMDUI_ROOT}")
    # 1. 告诉 CMake 和 CLion 语言服务器去哪里找 ZmdUI 模块
    list(APPEND QML_IMPORT_PATH "${ZMDUI_ROOT}")
    set(QML_IMPORT_PATH "${QML_IMPORT_PATH}" CACHE STRING "QML import paths" FORCE)

    # 2. 如果你需要把 ZmdUI 编译进资源，可以在这里添加它的 qrc（如果它有的话）
    # 大多数第三方 QML 组件库直接用文件系统路径即可，这里先不加进 qrc

    message(STATUS "qtClient: 已配置 ZmdUI 导入路径: ${ZMDUI_ROOT}/qml")
else ()
    message(WARNING "qtClient: 未找到 third_party/ZmdUI/qml，请检查目录结构！")
endif ()

qt_standard_project_setup()

# ── core：协议 / 网络 / 会话 / 日志（本工程自包含）─────────────────────
add_library(qtclient_core STATIC
        src/core/protocol.h
        src/core/protocol.cpp
        src/core/logger.h
        src/core/logger.cpp
        src/core/SessionManager.h
        src/core/SessionManager.cpp
        src/core/HttpClient.h
        src/core/HttpClient.cpp
        src/core/ChatSocket.h
        src/core/ChatSocket.cpp

        src/AccountValidator.h
        src/AccountValidator.cpp
        src/ChatController.h
        src/ChatController.cpp
        src/ContactController.h
        src/ContactController.cpp
        src/LoginController.h
        src/LoginController.cpp
        src/PresenceController.h
        src/PresenceController.cpp
        src/RegisterController.h
        src/RegisterController.cpp
        src/ResetPasswordController.h
        src/ResetPasswordController.cpp
)

target_include_directories(qtclient_core PUBLIC
        "src/core"
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_link_libraries(qtclient_core PUBLIC
        Qt6::Core
        Qt6::Network
)

# ── 应用（只含 QML 前端自身）──────────────────────────────────────────
qt_add_executable(qtClient
        src/main.cpp
        resources/qml.qrc
)

target_link_libraries(qtClient PRIVATE
        qtclient_core
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)

# ── 登录链路自检（无 GUI，命令行传 uid + 密码，用于自动化验证）──────────
# 复用的是同一个 LoginController，所以测的就是真实链路，不是另写一套逻辑。
qt_add_executable(qtClientLoginTest
        tests/autologin.cpp
)

target_link_libraries(qtClientLoginTest PRIVATE qtclient_core)

# ── 注册链路自检（需要验证码，用法见 tests/registerflow.cpp 顶部说明）────
qt_add_executable(qtClientRegisterTest
        tests/registerflow.cpp
)

target_link_libraries(qtClientRegisterTest PRIVATE qtclient_core)

# ── 重置密码链路自检（同样需要验证码）────────────────────────────────
qt_add_executable(qtClientResetTest
        tests/resetflow.cpp
)

target_link_libraries(qtClientResetTest PRIVATE qtclient_core)

# ── 联系人 / 好友链路自检（两段式：A 发申请 → B 收申请）────────────────
qt_add_executable(qtClientContactTest
        tests/contactflow.cpp
)

target_link_libraries(qtClientContactTest PRIVATE qtclient_core)

# ── 回包超时自检（自带"只收不发"的哑服务器，不需要真实服务端）──────────
# 验证的是"服务端某分支不回包时客户端会不会卡死"，这种场景没法靠真实服务端稳定复现。
qt_add_executable(qtClientSocketTimeoutTest
        tests/sockettimeout.cpp
)

target_link_libraries(qtClientSocketTimeoutTest PRIVATE qtclient_core)

# ── 消息发送自检（自带假 ChatServer，不需要真实服务端）────────────────
# 验证 1013 请求体的字段/类型、1014 成功与 1020 失败、串行发送、1022 推送、
# 以及"服务端一句都不回"时的超时收尾。真实服务端的回包信息量太少，测不出这些。
qt_add_executable(qtClientChatMsgTest
        tests/chatmessageflow.cpp
)

target_link_libraries(qtClientChatMsgTest PRIVATE qtclient_core)

# 这几个都是控制台程序：必须保留控制台窗口，否则 printf / stderr 的输出看不到
if (WIN32)
    set_target_properties(qtClientLoginTest PROPERTIES WIN32_EXECUTABLE FALSE)
    set_target_properties(qtClientRegisterTest PROPERTIES WIN32_EXECUTABLE FALSE)
    set_target_properties(qtClientResetTest PROPERTIES WIN32_EXECUTABLE FALSE)
    set_target_properties(qtClientContactTest PROPERTIES WIN32_EXECUTABLE FALSE)
    set_target_properties(qtClientSocketTimeoutTest PROPERTIES WIN32_EXECUTABLE FALSE)
    set_target_properties(qtClientChatMsgTest PROPERTIES WIN32_EXECUTABLE FALSE)
endif ()

# 配置文件：服务层按 applicationDirPath()/config.ini 读取 GateServer 地址
add_custom_command(TARGET qtClient POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/assets/config.ini"
        "$<TARGET_FILE_DIR:qtClient>"
        COMMENT "qtClient: 复制 config.ini 到构建目录"
        VERBATIM)

target_link_libraries(qtClient PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Network
)

# ── Windows：GUI 子系统 ────────────────────────────────────────────────
# MSVC 下是 /SUBSYSTEM:WINDOWS，MinGW 下 CMake 自动加 -mwindows，
# 两边的入口都由各自的 CRT 负责桥接到标准 main()。
if (WIN32)
    set_target_properties(qtClient PROPERTIES
            WIN32_EXECUTABLE $<NOT:$<BOOL:${QTCLIENT_CONSOLE}>>
    )
endif ()

# ── 编译警告：按编译器家族分支 ─────────────────────────────────────────
if (QTCLIENT_WARNINGS)
    if (MSVC)
        target_compile_options(qtClient PRIVATE /W4 /permissive-)
    else ()
        target_compile_options(qtClient PRIVATE -Wall -Wextra)
    endif ()
endif ()

# ── MinGW 运行库 ───────────────────────────────────────────────────────
# MinGW 编出来的 exe 默认依赖 libstdc++-6.dll / libgcc_s_*.dll / libwinpthread-1.dll，
# 这三个都不在 Qt 里，windeployqt 也不会帮你拷 —— 是最经典的"换台机器就报缺 DLL"。
# 两手准备：能静态链的静态链，剩下必须动态的（线程模型 DLL）拷到 exe 旁边。
if (MINGW)
    set(_qc_mingw_dlls libwinpthread-1.dll)
    if (QTCLIENT_STATIC_MINGW_RUNTIME)
        # 静态链 libgcc / libstdc++ 后这两个就不用拷了。
        # libwinpthread 是线程模型 DLL，静态链它容易出坑，所以照常拷。
        # 可用 objdump 复核导入表：objdump -p qtClient.exe | grep "DLL Name"
        target_link_options(qtClient PRIVATE -static-libgcc -static-libstdc++)
        message(STATUS "qtClient: MinGW 运行库静态链接已开启（libgcc / libstdc++）")
    else ()
        list(APPEND _qc_mingw_dlls
                libstdc++-6.dll libgcc_s_seh-1.dll libgcc_s_dw2-1.dll)
    endif ()

    # 编译器自带的运行时 DLL 就在 g++.exe 旁边
    get_filename_component(_qc_mingw_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    foreach (_dll ${_qc_mingw_dlls})
        if (EXISTS "${_qc_mingw_bin}/${_dll}")
            add_custom_command(TARGET qtClient POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${_qc_mingw_bin}/${_dll}"
                    "$<TARGET_FILE_DIR:qtClient>"
                    COMMENT "qtClient: 复制 MinGW 运行时 ${_dll}"
                    VERBATIM)
        endif ()
    endforeach ()
    unset(_qc_mingw_bin)
endif ()

# ── 构建目录自包含（Windows）：让 CLion 里直接 Run 就能跑 ───────────────
#
# Windows 上没有 RPATH，DLL 只从「exe 所在目录 / 系统目录 / 当前目录 / PATH」找。
# 刚编出来的 exe 旁边没有 Qt6Core.dll、PATH 里也没有 Qt 的 bin 时，CLion 一 Run
# 就立刻以 0xC0000135（STATUS_DLL_NOT_FOUND）退出 —— CLion 的 Run 不会帮你加
# Qt 的 bin。所以构建后把 Qt DLL + QML 插件 + 平台插件拷到 exe 旁边。
if (WIN32 AND QTCLIENT_DEPLOY_TO_BUILD_DIR)
    find_program(QTCLIENT_WINDEPLOYQT
            NAMES windeployqt
            HINTS "${QT6_INSTALL_PREFIX}/bin"
            NO_DEFAULT_PATH)
    if (NOT QTCLIENT_WINDEPLOYQT)
        find_program(QTCLIENT_WINDEPLOYQT NAMES windeployqt)
    endif ()

    if (QTCLIENT_WINDEPLOYQT)
        # 判断这个 Qt 套件到底有没有 Debug 版库：官方二进制套件（本机的 mingw_64 就是）
        # 只有 Release 的 Qt6Core.dll，没有 Qt6Cored.dll。这种情况下即使 Debug 构建，
        # 链接的也是 Release Qt，部署时必须给 --release —— 给 --debug 会去找不存在的
        # qwindowsd.dll，报 "Unable to find the platform plugin" 直接构建失败。
        if (EXISTS "${QT6_INSTALL_PREFIX}/bin/Qt6Cored.dll")
            set(_qc_qt_has_debug TRUE)
            message(STATUS "qtClient: Qt 套件带 Debug 库（Debug 构建按 --debug 部署）")
        else ()
            set(_qc_qt_has_debug FALSE)
            message(STATUS "qtClient: Qt 套件只有 Release 库（Debug 构建也按 --release 部署）")
        endif ()

        # 注意：必须是**单个** $<IF:...> 生成器表达式。
        # 写成两个 $<$<CONFIG:Debug>:--debug> 的话，条件不成立的那个会展开成一个空参数
        # （字面量 ""），windeployqt 会把它当成文件名直接报错。
        #
        # --qmldir 必须显式给：本工程用普通 .qrc 而非 qt_add_qml_module，
        # 部署工具不会自动知道 QML 在哪，漏掉会导致 qml/ 目录为空、程序起不来。
        add_custom_command(TARGET qtClient POST_BUILD
                COMMAND "${QTCLIENT_WINDEPLOYQT}"
                "$<IF:$<AND:$<CONFIG:Debug>,$<BOOL:${_qc_qt_has_debug}>>,--debug,--release>"
                --no-translations      # 没有翻译，省 3MB 与几百个小文件
                --no-opengl-sw         # D3D11 是默认渲染后端，opengl32sw 那 20MB 用不上
                --qmldir "${CMAKE_CURRENT_SOURCE_DIR}"
                "$<TARGET_FILE_DIR:qtClient>"
                COMMENT "qtClient: 部署 Qt 运行时到构建目录（CLion 里可直接 Run）"
                VERBATIM)
        message(STATUS "qtClient: 构建目录自包含已开启（windeployqt: ${QTCLIENT_WINDEPLOYQT}）")
    else ()
        message(WARNING
                "qtClient: 没找到 windeployqt，构建目录里的 exe 需要手动把 Qt 的 bin 加进 PATH。")
    endif ()
endif ()
