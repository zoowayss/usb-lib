cmake_minimum_required(VERSION 3.16)
project(usb-redirector VERSION 1.0.0 LANGUAGES CXX)

# 设置C++标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -DDEBUG")

# 检测操作系统
if(APPLE)
    set(BUILD_SENDER ON)
    set(BUILD_RECEIVER OFF)
    message(STATUS "Building for macOS - Sender enabled")
elseif(UNIX AND NOT APPLE)
    set(BUILD_SENDER OFF)
    set(BUILD_RECEIVER ON)
    message(STATUS "Building for Linux - Receiver enabled")
else()
    message(FATAL_ERROR "Unsupported platform")
endif()

# 查找依赖库
find_package(PkgConfig REQUIRED)

# libusb (发送端需要)
if(BUILD_SENDER)
    pkg_check_modules(LIBUSB REQUIRED IMPORTED_TARGET libusb-1.0)
    find_library(IOKIT_FRAMEWORK IOKit)
    find_library(COREFOUNDATION_FRAMEWORK CoreFoundation)
endif()

# 线程库
find_package(Threads REQUIRED)

# 包含目录
include_directories(${CMAKE_SOURCE_DIR})
include_directories(${CMAKE_SOURCE_DIR}/common)

# 公共库
add_subdirectory(common)

# 发送端 (macOS)
if(BUILD_SENDER)
    add_subdirectory(sender)
endif()

# 接收端 (Linux)
if(BUILD_RECEIVER)
    add_subdirectory(receiver)
endif()

# 测试
enable_testing()
add_subdirectory(tests)
