cmake_minimum_required(VERSION 3.24)

project(FrameXT VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Qt 元对象 / 资源编译
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# MSVC：源码与执行字符集统一 UTF-8（源文件含中文注释与 tr() 文案）
if(MSVC)
    add_compile_options(/utf-8)
endif()

# 外部依赖路径由 CMakePresets.json 的 win-base 预设提供：
#   CMAKE_PREFIX_PATH    -> Qt 6.11.1（msvc2022_64）
#   CMAKE_TOOLCHAIN_FILE -> vcpkg（exiv2 自 M2 元数据里程碑启用）

add_subdirectory(src/platform)
add_subdirectory(src/core)
add_subdirectory(src/app)

enable_testing()
add_subdirectory(tests)
