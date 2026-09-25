# =============================================================================
#  ChatClient —— Qt 6 桌面客户端（Qt Widgets）
#
#  构建系统：CMake（本工程唯一构建入口，不再维护 Visual Studio 工程文件）
#  工具链  ：Qt 6.11.2 MinGW 版 + MinGW-w64 GCC 13.1（x86_64-posix-seh）
#
#  命令行验证：
#    cmake --preset mingw-release
#    cmake --build --preset mingw-release
#  CLion：直接"打开"本目录，构建预设选 mingw-debug / mingw-release
#         （预设定义见同目录 CMakePresets.json，已写死编译器与 Qt 路径）
#
#  ⚠ 工具链必须成对配：Qt 的 MinGW 预编译包与 MinGW 版本是绑死的，
#    6.11.2/mingw_64 只能配 C:/dev/Qt/Tools/mingw1310_64（GCC 13.1）。
#    错配成 mingw1120_64（GCC 11.2）会因 libstdc++ ABI 不兼容而链接失败。
#  ⚠ Qt 官方 MinGW 包只提供 Release 版库（无 Qt6Cored.dll / libQt6Cored.a）。
#    故本工程默认构建类型为 Release；mingw-debug 预设只影响本工程自身的
#    调试信息与日志开关，链接的仍是 Release 版 Qt 库。
# =============================================================================

cmake_minimum_required(VERSION 3.21)

project(ChatClient
    VERSION 0.1.0
    DESCRIPTION "Qt6 聊天客户端"
    LANGUAGES CXX
)

# Win32 资源脚本需要 RC 编译器（MSVC 用 rc.exe，MinGW 用 windres.exe）。
# 保留 enable_language 是为了将来往 ChatClient.rc 里补入真实资源时能直接生效；
# 当前 ChatClient.rc 不含任何资源、不参与编译，原因见下方 CHATCLIENT_WIN32_RESOURCES。
if(WIN32)
    enable_language(RC)
endif()

# -----------------------------------------------------------------------------
# 全局编译设置
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # 产出 compile_commands.json，供 CLion 索引

# Qt 代码生成全自动：AUTOMOC / AUTOUIC / AUTORCC 按源文件依赖自动调度，
# 不再需要像 Qt VS Tools 那样手工维护 QtMoc / QtUic / QtRcc 清单
# （SearchEdit.h 之前就是漏登记 QtMoc 而缺少 moc）
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# .ui 里 <header location="global"> 提升的控件按 include 路径查找。
# 源文件按分层收进 src/ 后，把 src/ 设为 AUTOUIC 的搜索根，
# 于是 .ui 里写的 "ui/widgets/ClickedButton.h" 能被正确解析。
set(CMAKE_AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/src)

# 可执行文件统一输出到 <build>/bin，windeployqt 部署的 Qt DLL 也落在同目录，
# 这样在 CLion 里点运行就能直接起来
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# -----------------------------------------------------------------------------
# Qt：Core / Gui / Widgets / Network
#
# 优先用 -DCMAKE_PREFIX_PATH=<Qt 安装前缀>（CMakePresets.json 里已填好）。
# 未显式指定时，按当前工具链自动探测本机常见的 Qt 安装位置 —— 只探测与工具链
# ABI 一致的那一类（MSVC 找 msvc 版，GCC 找 mingw 版），避免编译期报
# "built with a different compiler" 之类的错误。
# 下面几行是本机路径提示，换机器或不想留可以整段删掉。
# -----------------------------------------------------------------------------
if(NOT CMAKE_PREFIX_PATH AND NOT DEFINED Qt6_DIR AND NOT DEFINED ENV{Qt6_DIR})
    if(MSVC)
        set(_chatclient_qt_hints
            "C:/dev/Qt/6.11.2/msvc2022_64"
            "C:/dev/Qt/6.5.3/msvc2019_64")
    else()
        set(_chatclient_qt_hints
            "C:/dev/Qt/6.11.2/mingw_64"
            "C:/dev/Qt/6.5.3/mingw_64")
    endif()
    foreach(_chatclient_qt_hint IN LISTS _chatclient_qt_hints)
        if(EXISTS "${_chatclient_qt_hint}/lib/cmake/Qt6/Qt6Config.cmake")
            list(APPEND CMAKE_PREFIX_PATH "${_chatclient_qt_hint}")
            message(STATUS "未指定 Qt 路径，自动检测到: ${_chatclient_qt_hint}")
        endif()
    endforeach()
endif()

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Network)

# -----------------------------------------------------------------------------
# 可执行文件
#   WIN32 → 走 Windows 子系统，不弹控制台窗口（MSVC 下即 -mwindows / SubSystem:Windows）
# -----------------------------------------------------------------------------
set(CHATCLIENT_SOURCES
    # 入口
    src/main.cpp

    # ── 表现层 ui ──
    # 主窗口外壳（承载页面切换）
    src/ui/mainwindow.h              src/ui/mainwindow.cpp              src/ui/mainwindow.ui
    # 页面（每个页面一个子目录）
    src/ui/page/chat/chatpage.h              src/ui/page/chat/chatpage.cpp              src/ui/page/chat/chatpage.ui
    src/ui/page/login/logindialog.h          src/ui/page/login/logindialog.cpp          src/ui/page/login/logindialog.ui
    src/ui/page/register/registerdialog.h    src/ui/page/register/registerdialog.cpp    src/ui/page/register/registerdialog.ui
    src/ui/page/reset/resetdialog.h          src/ui/page/reset/resetdialog.cpp          src/ui/page/reset/resetdialog.ui
    # 通用组件
    src/ui/components/ClickedButton.h   src/ui/components/ClickedButton.cpp
    src/ui/components/clicked_label.h   src/ui/components/clicked_label.cpp
    src/ui/components/timer_button.h    src/ui/components/timer_button.cpp
    # src/ui/components/SearchEdit.{h,cpp} 暂不参与编译，见下方 CHATCLIENT_PENDING_SOURCES

    # ── 网络层 network ──
    src/network/httpmanager.h        src/network/httpmanager.cpp
    src/network/TCPManager.h         src/network/TCPManager.cpp

    # ── 数据层 data ──
    src/data/UserManager.h           src/data/UserManager.cpp

    # ── 基础设施 core ──
    src/core/logger.h                src/core/logger.cpp

    # ── 应用级 app（全局配置与单例）──
    src/app/global.h                 src/app/global.cpp
    src/app/singleton.h

    # Qt 资源（图标 + stylesheet.qss；实体在 assets/ 下，由 rc.qrc 的 alias 映射）
    rc.qrc
)

add_executable(ChatClient WIN32 ${CHATCLIENT_SOURCES})

# -----------------------------------------------------------------------------
# 尚未完成的源码 —— 保留在工程内供 IDE 显示，暂不参与编译
#
# SearchEdit.h 声明了构造函数 SearchEdit(QWidget*)、虚函数 focusOutEvent()
# 以及 setMaxLength() / limitTextLength()，但 SearchEdit.cpp 目前只有一行
# #include，四个成员都没有实现。AUTOMOC 生成的 moc 代码会引用构造函数与 vtable，
# 因此在实现补齐之前链接必然失败：
#   undefined reference to `SearchEdit::SearchEdit(QWidget*)'
#   undefined reference to `SearchEdit::focusOutEvent(QFocusEvent*)'
#
# 注意这与本次迁移无关：源工程（Visual Studio / MSVC）用的是同一份未完成源码，
# 同样无法链接通过。移出编译只是为了让工程先处于"可构建、可运行"的干净状态。
#
# ★ 继续开发 SearchEdit 时：补完实现后，把 SearchEdit.h / SearchEdit.cpp
#   两行移回上面的 CHATCLIENT_SOURCES 即可，其余无需改动。
# -----------------------------------------------------------------------------
set(CHATCLIENT_PENDING_SOURCES
    src/ui/components/SearchEdit.h
    src/ui/components/SearchEdit.cpp
)
# HEADER_FILE_ONLY 只挡住"编译"，挡不住 AUTOMOC —— 后者照样会为 SearchEdit.h
# 生成 moc_SearchEdit.cpp 并塞进 mocs_compilation.cpp，链接依旧报未定义引用。
# 所以两个属性必须同时设。
set_source_files_properties(${CHATCLIENT_PENDING_SOURCES} PROPERTIES
    HEADER_FILE_ONLY TRUE   # 不参与编译
    SKIP_AUTOMOC    TRUE    # 也不让 AUTOMOC 为它生成 moc
)
target_sources(ChatClient PRIVATE ${CHATCLIENT_PENDING_SOURCES})

# -----------------------------------------------------------------------------
# Win32 资源脚本 —— 保留在工程内供 IDE 显示，但不参与编译
#
# ChatClient.rc 由 VS 资源编辑器生成，是"空壳"：除一条 LANGUAGE 语句外没有任何
# 真实资源定义（所有 TEXTINCLUDE 块都包在 #ifdef APSTUDIO_INVOKED 里，只有
# VS 资源编辑器会定义该宏）。MSVC 的 rc.exe 接受空资源文件，binutils 的 windres
# 却会以 "no resources" 报错退出（实测 2.39 与 2.47 两个版本均如此），
# 直接导致 MinGW 构建在这一步中断。
#
# 该文件本就零资源贡献（图标与 stylesheet.qss 全部由 rc.qrc 经 Qt 资源系统打包），
# 因此移出编译列表；文件仍留在源码目录，不删不改。
# 将来需要版本信息 / 应用图标等 Win32 资源时：在 ChatClient.rc 里补上真实资源定义，
# 再把它加回上面的 CHATCLIENT_SOURCES 即可（届时 windres 有资源可编，自然通过）。
# -----------------------------------------------------------------------------
set(CHATCLIENT_WIN32_RESOURCES
    packaging/win32/ChatClient.rc
    packaging/win32/resource.h
)
set_source_files_properties(${CHATCLIENT_WIN32_RESOURCES} PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(ChatClient PRIVATE ${CHATCLIENT_WIN32_RESOURCES})

# 源文件按分层收进 src/，统一以 src/ 为头文件搜索根：
# 代码里写 #include "core/logger.h"、"ui/widgets/ClickedButton.h"
target_include_directories(ChatClient PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(ChatClient PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Network
)

# Windows GUI 子系统下，main() 需要入口点库桥接到 WinMain。
# MinGW 下这个入口点库同样是必需的，不能靠 GCC 的 -mwindows 代替。
if(WIN32 AND TARGET Qt6::EntryPoint)
    target_link_libraries(ChatClient PRIVATE Qt6::EntryPoint)
endif()

# -----------------------------------------------------------------------------
# 编译选项：两套工具链各给一份等价配置
# -----------------------------------------------------------------------------
if(MSVC)
    target_compile_options(ChatClient PRIVATE
        /W3             # <WarningLevel>Level3</WarningLevel>
        /permissive-    # <ConformanceMode>true</ConformanceMode>
        /wd4996         # <DisableSpecificWarnings>4996</DisableSpecificWarnings>
        /utf-8          # 源码为 UTF-8 无 BOM，强制按 UTF-8 解析
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    # MinGW / GCC 的等价项。源码是 UTF-8 无 BOM 且含中文注释与中文字面量，
    # 显式钉死输入/执行字符集，避免不同区域设置下出现中文乱码。
    target_compile_options(ChatClient PRIVATE
        -Wall
        -Wno-deprecated-declarations   # 对应 MSVC 的 /wd4996
        -finput-charset=UTF-8
        -fexec-charset=UTF-8
    )
endif()

target_compile_definitions(ChatClient PRIVATE
    UNICODE _UNICODE                                   # <CharacterSet>Unicode</CharacterSet>
    QT_MESSAGELOGCONTEXT                               # 日志前缀的 文件:行 在两种配置下都可用
    $<$<CONFIG:Debug>:CHATCLIENT_LOG_ENABLED>          # Debug：输出 DEBUG 及以上
    $<$<NOT:$<CONFIG:Debug>>:CHATCLIENT_LOG_DISABLED>  # Release：LOG_DEBUG 编译期消除
)

# -----------------------------------------------------------------------------
# 构建后处理
# -----------------------------------------------------------------------------

# 1) config.ini 拷到 exe 同目录（main.cpp 从 applicationDirPath() 读它）
add_custom_command(TARGET ChatClient POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/config.ini"
            "$<TARGET_FILE_DIR:ChatClient>/config.ini"
    COMMENT "复制 config.ini 到输出目录"
    VERBATIM
)

# 2) 部署 Qt 运行库与平台插件到 exe 同目录。
#    本机 PATH 里没有 Qt bin，CLion 也不会像 Qt VS Tools 那样给调试会话注入环境，
#    所以把 DLL / 插件直接放到 exe 旁边，CLion 里点运行就能起来。
#    不需要时：cmake -DCHATCLIENT_DEPLOY_QT=OFF
option(CHATCLIENT_DEPLOY_QT "构建后把 Qt 运行库与平台插件部署到输出目录" ON)

if(WIN32 AND CHATCLIENT_DEPLOY_QT)
    # 推导三个目录，交给部署脚本使用：
    #   Qt6_DIR = <Qt 前缀>/lib/cmake/Qt6  →  上溯三级即 Qt 安装前缀
    get_filename_component(_chatclient_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    set(_chatclient_qt_bin     "${_chatclient_qt_prefix}/bin")
    set(_chatclient_qt_plugins "${_chatclient_qt_prefix}/plugins")

    # 编译器所在目录：MinGW 下这里有 libgcc_s_seh-1.dll / libstdc++-6.dll /
    # libwinpthread-1.dll，以及部署脚本要用的 objdump。
    get_filename_component(_chatclient_toolchain_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    find_program(CHATCLIENT_OBJDUMP_EXECUTABLE
        NAMES objdump
        HINTS "${_chatclient_toolchain_bin}"
    )

    if(NOT CHATCLIENT_OBJDUMP_EXECUTABLE)
        message(WARNING
            "未找到 objdump，无法自动部署 Qt 运行库。"
            "请把 Qt 的 bin 目录与 MinGW 的 bin 目录加入 PATH 后再运行，"
            "或设 -DCHATCLIENT_DEPLOY_QT=OFF 关闭本步骤。")
    else()
        # 要部署的 Qt 插件类别（逗号分隔；用分号会被 CMake 当列表分隔符拆散）。
        #   platforms     窗口系统，qwindows 缺失直接起不来
        #   styles        控件外观，qmodernwindowsstyle 缺失会静默退回老旧的 Fusion 样式
        #   imageformats  图片格式（jpg/webp/gif/ico 等；png 是内置的）
        #   iconengines   SVG 图标支持
        set(CHATCLIENT_QT_PLUGIN_GROUPS "platforms,styles,imageformats,iconengines"
            CACHE STRING "构建后要部署的 Qt 插件类别（逗号分隔）")

        add_custom_command(TARGET ChatClient POST_BUILD
            COMMAND ${CMAKE_COMMAND}
                    "-DEXE=$<TARGET_FILE:ChatClient>"
                    "-DOBJDUMP=${CHATCLIENT_OBJDUMP_EXECUTABLE}"
                    "-DQT_BIN=${_chatclient_qt_bin}"
                    "-DTOOLCHAIN_BIN=${_chatclient_toolchain_bin}"
                    "-DQT_PLUGINS=${_chatclient_qt_plugins}"
                    "-DPLUGIN_GROUPS=${CHATCLIENT_QT_PLUGIN_GROUPS}"
                    -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/DeployQtRuntime.cmake"
            COMMENT "部署 Qt 运行库与插件到 $<TARGET_FILE_DIR:ChatClient>"
            VERBATIM
        )
    endif()
endif()

# -----------------------------------------------------------------------------
# IDE 集成：用 "打开文件夹" 方式在 VS 里打开本工程时，启动项目选 ChatClient
# -----------------------------------------------------------------------------
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT ChatClient)

# -----------------------------------------------------------------------------
# 可选的协议一致性测试（tests/ 目录，自带独立 CMakeLists，默认不参与构建）
#   cmake --preset mingw-release -DCHATCLIENT_BUILD_TESTS=ON
# -----------------------------------------------------------------------------
option(CHATCLIENT_BUILD_TESTS "同时构建 tests/ 下的协议一致性测试" OFF)
if(CHATCLIENT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
