cmake_minimum_required(VERSION 3.16)

project(deepseek_balance_cube_gtk
    VERSION 1.0.0
    DESCRIPTION "DeepSeek 余额小方块（Linux / GTK3 / cairo）"
    LANGUAGES CXX)

# 这份源码只跑 Linux。Windows 版在上一层的 src/，两者互不依赖。
if(NOT UNIX OR APPLE)
    message(FATAL_ERROR "linux/ 只支持 Linux（依赖 GTK3 / cairo / GIO）。")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "构建类型" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
endif()

# gtk+-3.0 的 pkg-config 已经把 gio-2.0 / pangocairo / cairo / glib 都带出来了，不用单列。
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)

set(APP_SOURCES
    src/main.cpp
    src/util.cpp
    src/json.cpp
    src/peak.cpp
    src/pacer.cpp
    src/balance.cpp
    src/sound.cpp
    src/render.cpp
    src/cube.cpp)

add_executable(deepseek_balance_cube ${APP_SOURCES})

target_include_directories(deepseek_balance_cube PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# 不加 -Werror：GTK3 的头文件本身就会带出一堆 deprecated 警告，加了两边打架。
target_compile_options(deepseek_balance_cube PRIVATE -Wall -Wextra -Wpedantic)
target_compile_definitions(deepseek_balance_cube PRIVATE GDK_VERSION_MIN_REQUIRED=GDK_VERSION_3_22)

target_link_libraries(deepseek_balance_cube PRIVATE PkgConfig::GTK3)

# 单配置生成器和多配置生成器都让产物落在 <build>/bin 下，找起来方便。
foreach(_cfg "" _DEBUG _RELEASE _RELWITHDEBINFO _MINSIZEREL)
    set_target_properties(deepseek_balance_cube PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY${_cfg} "${CMAKE_BINARY_DIR}/bin")
endforeach()

# 注意：这里绝不能学 Windows 版加 -static。GTK3 依赖一堆系统库和运行期加载的
# gio 模块（libgiognutls.so 等），静态链接既做不到也没有意义。
