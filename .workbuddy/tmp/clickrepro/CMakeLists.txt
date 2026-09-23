cmake_minimum_required(VERSION 3.21)
project(ClickRepro LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

set(SRC_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../..)
set(CMAKE_AUTOUIC_SEARCH_PATHS ${SRC_ROOT})

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)

add_executable(clickrepro
    main.cpp
    ${SRC_ROOT}/logger.h
    ${SRC_ROOT}/logger.cpp
    ${SRC_ROOT}/global.h
    ${SRC_ROOT}/global.cpp
    ${SRC_ROOT}/httpmanager.h
    ${SRC_ROOT}/httpmanager.cpp
    ${SRC_ROOT}/clicked_label.h
    ${SRC_ROOT}/clicked_label.cpp
    ${SRC_ROOT}/timer_button.h
    ${SRC_ROOT}/timer_button.cpp
    ${SRC_ROOT}/registerdialog.h
    ${SRC_ROOT}/registerdialog.cpp
    ${SRC_ROOT}/registerdialog.ui
)

target_include_directories(clickrepro PRIVATE ${SRC_ROOT})
target_link_libraries(clickrepro PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network)
target_compile_definitions(clickrepro PRIVATE QT_MESSAGELOGCONTEXT CHATCLIENT_LOG_ENABLED)
