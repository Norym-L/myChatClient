# =============================================================================
#  DeployQtRuntime.cmake —— 构建后把 Qt 运行库与平台插件部署到 exe 同目录
#
#  由 CMakeLists.txt 以 `cmake -P` 方式在 POST_BUILD 阶段调用。
#
#  ── 为什么不用 windeployqt ──────────────────────────────────────────────
#  windeployqt 靠启动子进程查询 qtpaths / qmake 来确定 Qt 安装位置。在受限环境
#  （沙箱、CI、权限收紧的终端）下它会失败并只打印一行：
#      Unable to query qtpaths: Error running binary qtpaths: pipe:
#  然后静默跳过全部部署 —— 不报错、不拷贝，留下一个缺 DLL、双击起不来的 exe。
#  实测该环境下 `qtpaths --version` 本身能正常运行，问题出在 windeployqt 内部
#  的子进程/管道环节，排查成本很高。
#
#  ── 为什么也不用 file(GET_RUNTIME_DEPENDENCIES) ─────────────────────────
#  它会递归解析整棵依赖树，连 System32 里上百个系统 DLL 的依赖都要走一遍，实测
#  单次构建因此多花 5~6 分钟；而且它返回的依赖路径在 Windows 上用反斜杠分隔，
#  任何按 "/" 写的排除正则都会静默失效 —— 曾导致 24 个系统 DLL 被拷进 exe 同目录
#  （会被优先加载，造成版本冲突与 DLL 劫持风险）。
#
#  ── 本脚本的做法 ────────────────────────────────────────────────────────
#  用导入表读取工具（MinGW: objdump -p，MSVC: dumpbin /dependents）扫依赖，
#  并且只沿着"位于 Qt / 工具链目录下的 DLL"继续递归。
#  系统 DLL 不在我们这两个目录里，直接跳过、不递归 —— 既快（毫秒级）又天然是
#  白名单，与路径分隔符、大小写都无关。
#
#  ── 参数（由 CMakeLists.txt 在 configure 期拼好后传入）──────────────────
#    EXE            目标可执行文件路径
#    TOOL           导入表读取工具路径（objdump / dumpbin，与编译器同目录）
#    TOOL_KIND      objdump | dumpbin，省略时按 TOOL 文件名自动判定
#                   （旧参数名 OBJDUMP 仍可用，等价于 TOOL + TOOL_KIND=objdump）
#    QT_BIN         Qt 的 bin 目录（提供 Qt6*.dll）
#    TOOLCHAIN_BIN  编译器 bin 目录（提供 libgcc_s_*.dll / libstdc++-6.dll /
#                   libwinpthread-1.dll）
#    QT_PLUGINS     Qt 的 plugins 目录（用于补平台插件）
# =============================================================================

cmake_minimum_required(VERSION 3.21)

# 兼容旧调用：只传了 OBJDUMP 时，等价于 TOOL=OBJDUMP / TOOL_KIND=objdump
if(NOT DEFINED TOOL AND DEFINED OBJDUMP)
    set(TOOL "${OBJDUMP}")
endif()
if(NOT DEFINED TOOL_KIND OR "${TOOL_KIND}" STREQUAL "")
    get_filename_component(_tool_name "${TOOL}" NAME_WE)
    if(_tool_name STREQUAL "dumpbin")
        set(TOOL_KIND "dumpbin")
    else()
        set(TOOL_KIND "objdump")
    endif()
endif()

foreach(_required EXE TOOL QT_BIN QT_PLUGINS)
    if(NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "DeployQtRuntime: 缺少必需参数 ${_required}")
    endif()
endforeach()

get_filename_component(_dest_dir "${EXE}" DIRECTORY)

set(_search_dirs "${QT_BIN}")
if(DEFINED TOOLCHAIN_BIN AND NOT "${TOOLCHAIN_BIN}" STREQUAL "")
    list(APPEND _search_dirs "${TOOLCHAIN_BIN}")
endif()

# -----------------------------------------------------------------------------
# 从 EXE 出发做广度优先扫描；只对在 Qt / 工具链目录里找到的 DLL 继续递归
# -----------------------------------------------------------------------------
set(_queue   "${EXE}")
set(_visited "")
set(_count   0)

while(_queue)
    list(POP_FRONT _queue _current)

    if(TOOL_KIND STREQUAL "dumpbin")
        execute_process(
            COMMAND "${TOOL}" /dependents "${_current}"
            OUTPUT_VARIABLE _dump
            ERROR_QUIET
            RESULT_VARIABLE _rc
        )
    else()
        execute_process(
            COMMAND "${TOOL}" -p "${_current}"
            OUTPUT_VARIABLE _dump
            ERROR_QUIET
            RESULT_VARIABLE _rc
        )
    endif()
    if(NOT _rc EQUAL 0)
        continue()
    endif()

    if(TOOL_KIND STREQUAL "dumpbin")
        # dumpbin /dependents 的输出形如 "    Qt6Widgets.dll"（纯文件名一行一个）。
        # 这里放宽成"任意 *.dll 词"：误报的代价为零 —— 后面只会在 Qt / 工具链目录里
        # 按名字找，找不到的（含 System32 的系统 DLL）直接跳过。
        string(REGEX MATCHALL "[^ \t\r\n\\/:]+\\.[Dd][Ll][Ll]" _names "${_dump}")
    else()
        # objdump -p 的导入表形如 "	DLL Name: Qt6Widgets.dll"
        string(REGEX MATCHALL "DLL Name: [^ \t\r\n]+" _entries "${_dump}")
        set(_names "")
        foreach(_entry IN LISTS _entries)
            string(REPLACE "DLL Name: " "" _n "${_entry}")
            list(APPEND _names "${_n}")
        endforeach()
    endif()

    foreach(_name IN LISTS _names)
        string(TOLOWER "${_name}" _name_lc)

        if(_name_lc STREQUAL "")
            continue()
        endif()
        if(_name_lc IN_LIST _visited)
            continue()
        endif()
        list(APPEND _visited "${_name_lc}")

        # 只在我们自己的目录里找；找不到的就是系统 DLL，跳过且不递归
        foreach(_dir IN LISTS _search_dirs)
            if(EXISTS "${_dir}/${_name}")
                file(COPY "${_dir}/${_name}" DESTINATION "${_dest_dir}")
                message(STATUS "  已部署运行库: ${_name}")
                math(EXPR _count "${_count} + 1")
                list(APPEND _queue "${_dir}/${_name}")
                break()
            endif()
        endforeach()
    endforeach()
endwhile()

# -----------------------------------------------------------------------------
# Qt 插件是运行时按名字动态加载的，不会出现在导入表里，必须显式部署。
# 按类别整目录拷贝，类别清单由 CMakeLists.txt 通过 PLUGIN_GROUPS 传入。
#
# ★ 为什么 styles 不能漏：
#   Qt 6 在 Windows 上默认使用 qmodernwindowsstyle（原生 Windows 11 控件外观）。
#   该插件缺失时 Qt 不会报错，而是**静默退回内置的 Fusion 样式** —— 控件立刻
#   变成"老版 Windows"的观感。因为是静默降级，很容易被误判成界面设计问题，
#   实际只是插件没部署。
# -----------------------------------------------------------------------------
if(NOT DEFINED PLUGIN_GROUPS OR "${PLUGIN_GROUPS}" STREQUAL "")
    set(PLUGIN_GROUPS "platforms,styles")
endif()
# 用逗号分隔的好处：分号在 CMake 参数里是列表分隔符，传递时会被拆散
string(REPLACE "," ";" PLUGIN_GROUPS "${PLUGIN_GROUPS}")

foreach(_group IN LISTS PLUGIN_GROUPS)
    if(NOT IS_DIRECTORY "${QT_PLUGINS}/${_group}")
        message(STATUS "  跳过不存在的插件类别: ${_group}")
        continue()
    endif()

    file(GLOB _plugins "${QT_PLUGINS}/${_group}/*.dll")
    list(LENGTH _plugins _n)
    foreach(_plugin IN LISTS _plugins)
        file(COPY "${_plugin}" DESTINATION "${_dest_dir}/${_group}")
        math(EXPR _count "${_count} + 1")
    endforeach()
    message(STATUS "  已部署插件类别 ${_group}：${_n} 个")
endforeach()

message(STATUS "Qt 运行库部署完成：${_count} 个文件 -> ${_dest_dir}")
