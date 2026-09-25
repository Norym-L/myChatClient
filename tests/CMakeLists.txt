# 协议一致性测试（不依赖 Visual Studio 工程，可单独构建）
#
# 用法：
#   cmake -S tests -B <build-dir> -DCMAKE_PREFIX_PATH=<Qt6 安装路径>
#   cmake --build <build-dir>
#   set QT_ASSUME_STDERR_HAS_CONSOLE=1 && <build-dir>/protocol_conformance
#
# 注意：需要与 Qt 安装一致地选择 CMAKE_BUILD_TYPE（Qt 官方 mingw 包只有 Release 库）。
#
# 测试内容：用本地 mock 服务端校验客户端对各接口的
#   请求地址 / 请求方式 / 请求参数字段 / 响应字段与错误码处理
# 是否与 ServerDocs 下的接口文档一致。

cmake_minimum_required(VERSION 3.21)
project(ChatClientProtocolTest LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

set(SRC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
# 主工程源码按分层收在 src/ 下，这里同样以 src/ 为头文件搜索根
set(SRC_DIR  ${SRC_ROOT}/src)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${SRC_DIR})

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(protocol_conformance
    protocol_conformance.cpp
    ${SRC_DIR}/core/logger.h
    ${SRC_DIR}/core/logger.cpp
    ${SRC_DIR}/app/global.h
    ${SRC_DIR}/app/global.cpp
    ${SRC_DIR}/network/httpmanager.h
    ${SRC_DIR}/network/httpmanager.cpp
    ${SRC_DIR}/network/TCPManager.h
    ${SRC_DIR}/network/TCPManager.cpp
    ${SRC_DIR}/data/UserManager.h
    ${SRC_DIR}/data/UserManager.cpp
    ${SRC_DIR}/ui/components/clicked_label.h
    ${SRC_DIR}/ui/components/clicked_label.cpp
    ${SRC_DIR}/ui/components/timer_button.h
    ${SRC_DIR}/ui/components/timer_button.cpp
    ${SRC_DIR}/ui/page/login/logindialog.h
    ${SRC_DIR}/ui/page/login/logindialog.cpp
    ${SRC_DIR}/ui/page/login/logindialog.ui
    ${SRC_DIR}/ui/page/register/registerdialog.h
    ${SRC_DIR}/ui/page/register/registerdialog.cpp
    ${SRC_DIR}/ui/page/register/registerdialog.ui
    ${SRC_DIR}/ui/page/reset/resetdialog.h
    ${SRC_DIR}/ui/page/reset/resetdialog.cpp
    ${SRC_DIR}/ui/page/reset/resetdialog.ui
)

target_include_directories(protocol_conformance PRIVATE ${SRC_DIR})
target_link_libraries(protocol_conformance PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)

# 与 ChatClient.vcxproj 保持一致的日志开关：Debug 输出全部级别，Release 只输出 INFO 及以上
# QT_MESSAGELOGCONTEXT 保证日志前缀里的 文件:行 在所有配置下都可用
target_compile_definitions(protocol_conformance PRIVATE
    QT_MESSAGELOGCONTEXT
    $<$<CONFIG:Debug>:CHATCLIENT_LOG_ENABLED>
    $<$<NOT:$<CONFIG:Debug>>:CHATCLIENT_LOG_DISABLED>
)
