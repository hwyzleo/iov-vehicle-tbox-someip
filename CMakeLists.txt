cmake_minimum_required(VERSION 3.16)
project(tbox-someip VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
add_compile_options(-Wall -g)

# TBox Framework：统一通过 find_package 消费（自包含 yaml-cpp / spdlog）
# 查找前缀由 scripts/build.sh 从 iov-vehicle-tbox-framework/scripts/tbox-env.sh
# 取得的 TBOX_PREFIX 决定，切换本地/生产环境无需改动本文件。
if(NOT TARGET TBoxFramework::tbox-framework)
    find_package(TBoxFramework REQUIRED)
endif()

# SOME/IP 实现选项
option(USE_MOCK_SOMEIP "Use mock SOME/IP implementation instead of vsomeip" ON)

# 内部 IPC 接入选项 (CR-005)
option(USE_REAL_IPC "Use real framework-ipc domain client facades (tsp_client) instead of mock" OFF)

if(USE_REAL_IPC)
    message(STATUS "Using real framework-ipc domain client facades")
    add_definitions(-DUSE_REAL_IPC)
    find_package(TboxTspClient REQUIRED)
endif()

if(USE_MOCK_SOMEIP)
    message(STATUS "Using mock SOME/IP implementation")
    add_definitions(-DUSE_MOCK_SOMEIP)
else()
    message(STATUS "Using real vsomeip implementation")
    
    # 查找依赖
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(VSOMEIP REQUIRED vsomeip3)
    pkg_check_modules(COMMONAPI REQUIRED CommonAPI)
    pkg_check_modules(COMMONAPI_SOMEIP REQUIRED CommonAPI-SomeIP)
endif()

# 选项
option(BUILD_TESTS "Build tests" ON)

# 子目录
add_subdirectory(src)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
