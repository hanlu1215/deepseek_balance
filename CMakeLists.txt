cmake_minimum_required(VERSION 3.16)

project(deepseek_balance_cube
    VERSION 1.0.0
    DESCRIPTION "DeepSeek 余额小方块（Win32 / GDI+ / WinHTTP）"
    LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "deepseek_balance_cube 只支持 Windows（依赖 Win32 / GDI+ / WinHTTP / WinMM）。")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# ------------------------------------------------------------------ 图标与资源
# exe 图标就用仓库根目录的 ks.ico；资源脚本从模板生成，把绝对路径塞进去，
# 这样无论从哪个目录构建都能找得到图标和清单。
#
# 路径一律用正斜杠：windres 会把反斜杠当转义序列（\r 变回车、\a 变响铃），
# 而 rc.exe 又不会反转义 \\。正斜杠两边都认，最省事。
set(APP_ICON "${CMAKE_CURRENT_SOURCE_DIR}/ks.ico")
set(APP_MANIFEST_RC "${CMAKE_CURRENT_SOURCE_DIR}/src/resources/app.manifest")

if(EXISTS "${APP_ICON}")
    set(APP_ICON_LINE "IDI_APPICON ICON \"${APP_ICON}\"")
else()
    # 注意：仓库的 .gitignore 里有 *.ico，ks.ico 默认是不进版本库的。
    # 别人克隆下来就可能没有这个文件，所以这里只警告不报错，照常出 exe。
    set(APP_ICON_LINE "// 没找到 ks.ico，本次构建不带图标资源")
    message(WARNING "找不到图标 ${APP_ICON}，生成的 exe 将没有自定义图标。"
                    "想带上图标：把 ks.ico 放到项目根目录，或改 CMakeLists.txt 里的 APP_ICON。")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/resources/app.rc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
    @ONLY)

# ------------------------------------------------------------------ 目标
set(APP_SOURCES
    src/main.cpp
    src/util.cpp
    src/json.cpp
    src/peak.cpp
    src/pacer.cpp
    src/balance.cpp
    src/sound.cpp
    src/render.cpp
    src/cube.cpp
    "${CMAKE_CURRENT_BINARY_DIR}/app.rc")

add_executable(deepseek_balance_cube WIN32 ${APP_SOURCES})

target_include_directories(deepseek_balance_cube PRIVATE src)

target_compile_definitions(deepseek_balance_cube PRIVATE
    UNICODE _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _WIN32_WINNT=0x0601
    WINVER=0x0601
    _CRT_SECURE_NO_WARNINGS)

target_link_libraries(deepseek_balance_cube PRIVATE
    gdiplus     # 抗锯齿圆角卡片 + 文字
    winhttp     # 余额接口
    winmm       # PlaySound 木鱼音效
    user32
    gdi32
    shell32)    # CommandLineToArgvW

# 多配置生成器（Visual Studio）没有 CMAKE_BUILD_TYPE，四个后缀都要设一遍，
# exe 才会统一落在 <build>/bin 下，找起来方便。
foreach(_cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(deepseek_balance_cube PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY${_cfg} "${CMAKE_BINARY_DIR}/bin")
endforeach()

if(MSVC)
    # /utf-8：源码是 UTF-8，里面全是中文字面量，不加这个 MSVC 会按 GBK 解析
    target_compile_options(deepseek_balance_cube PRIVATE /utf-8 /W3 /permissive-)
    # 清单已经由 app.rc 以 RT_MANIFEST 提供，关掉链接器自动生成的那一份，免得冲突
    target_link_options(deepseek_balance_cube PRIVATE /MANIFEST:NO)
else()
    target_compile_options(deepseek_balance_cube PRIVATE -Wall -Wextra)

    # MinGW 默认把 libgcc_s_seh-1.dll / libstdc++-6.dll（以及 posix 线程模型下的
    # libwinpthread-1.dll）做成动态依赖，exe 拷到没装 MSYS2 的电脑上就起不来。
    # 全部静态链进去之后，剩下的依赖只有 kernel32/user32/gdiplus 这些系统自带的 DLL，
    # 真正做到「拷过去双击就能用」。
    target_link_options(deepseek_balance_cube PRIVATE
        -static
        -static-libgcc
        -static-libstdc++)
endif()
