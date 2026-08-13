# TBOX-SOMEIP-DSN-CR-006: 接入 TBOX-BUILD 统一 Orin aarch64 构建。
#   - 不设置 CMAKE_C_COMPILER / CMAKE_CXX_COMPILER / CMAKE_SYSROOT（由 BUILD
#     toolchain/preset 注入）
#   - framework / TSP / PROV 通过已安装 CMake package 消费（find_package CONFIG），
#     禁止 add_subdirectory、上游源码私有头文件、build-tree library 或绝对库路径
#   - GNUInstallDirs + 显式 install component someip-runtime（staging: rootfs）
#   - vsomeip/CommonAPI 按 HOST/TARGET 分离：generator 仅 HOST 执行，TARGET
#     runtime 来自 Orin sysroot 或受控 recipe（BUILD 经 TBOX_DEP_STAGING 注入）
#   - 无硬编码 RPATH / 宿主路径 / build-tree library

cmake_minimum_required(VERSION 3.16)
project(tbox-someip VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)

# ============================================================
# CR-006 构建开关
# ============================================================
# USE_MOCK_SOMEIP=OFF (默认): 真实 vsomeip/CommonAPI runtime（Orin 正式构建）。
#   ON: 本地开发/单测使用 mock vsomeip 实现（scripts/build.sh 显式传入）。
#   注意：此开关仅影响对外 SOME/IP（vsomeip）实现，与对内 IPC 无关。
#   对内 TSP/PROV 始终经 framework-ipc 域 client facade 接入真实后端，无 mock 桩。
option(USE_MOCK_SOMEIP "Use mock SOME/IP implementation instead of vsomeip" OFF)

# ============================================================
# TBOX SDK 消费（CR-006 §11.3：installed package/targets only）
# BUILD 环境经 toolchain 注入 CMAKE_PREFIX_PATH（SDK staging + dep staging）；
# 本地开发经 scripts/build.sh 注入 TBOX_PREFIX（framework tbox-env.sh）。
# 组件按 daemon 实际使用声明：config（hwyz::config）、log（tbox::fw::log）、
# ipc（tbox::fw::ipc Subscription，TSP/PROV adapter 使用）。
# ============================================================
if(NOT TARGET TBoxFramework::config)
    find_package(TboxFramework CONFIG REQUIRED)
endif()
set(SOMEIP_FRAMEWORK_LIBS
    TBoxFramework::config
    TBoxFramework::log
    TBoxFramework::ipc
)

# framework-ipc 域 client facade（始终链接：门面服务唯一的对内接入方式）
message(STATUS "Using framework-ipc domain client facades (tsp_client/prov_client)")
# TSP client（CR-005：FOTA 上行/下行桥接）
if(NOT TARGET tbox::tsp_client)
    find_package(TboxTspClient CONFIG REQUIRED)
endif()
# PROV client（CR-006：配置/绑定只读链路）
if(NOT TARGET tbox::prov_client)
    find_package(TboxProvClient CONFIG REQUIRED)
endif()

# ============================================================
# Proto / protobuf（CR-008：VEH-PROTO 为文档 SSOT，各服务自备所需 proto）
# - HOST：protoc 仅生成构建中间文件（不进入 runtime package）
# - TARGET：libprotobuf runtime 来自受控依赖（BUILD dep staging / pkg-config），
#   与 vsomeip 的 HOST/TARGET 分离模式一致（CR-006 §11.4）。
# ============================================================
find_program(PROTOC_EXECUTABLE NAMES protoc REQUIRED)
if(NOT TARGET PkgConfig::PROTOBUF)
    find_package(PkgConfig REQUIRED)
    set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)
    pkg_check_modules(PROTOBUF REQUIRED IMPORTED_TARGET protobuf)
endif()

set(PROTO_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated/proto")
file(GLOB_RECURSE PROTO_FILES CONFIGURE_DEPENDS "${PROTO_SRC_DIR}/*.proto")

set(PROTO_GEN_SOURCES "")
set(PROTO_GEN_HEADERS "")
foreach(PROTO_FILE ${PROTO_FILES})
    file(RELATIVE_PATH PROTO_REL "${PROTO_SRC_DIR}" "${PROTO_FILE}")
    string(REPLACE ".proto" ".pb.cc" PROTO_CC "${PROTO_REL}")
    string(REPLACE ".proto" ".pb.h" PROTO_H "${PROTO_REL}")
    set(PROTO_CC_ABS "${PROTO_GEN_DIR}/${PROTO_CC}")
    set(PROTO_H_ABS "${PROTO_GEN_DIR}/${PROTO_H}")
    list(APPEND PROTO_GEN_SOURCES "${PROTO_CC_ABS}")
    list(APPEND PROTO_GEN_HEADERS "${PROTO_H_ABS}")
    add_custom_command(
        OUTPUT "${PROTO_CC_ABS}" "${PROTO_H_ABS}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROTO_GEN_DIR}"
        COMMAND ${PROTOC_EXECUTABLE} --proto_path=${PROTO_SRC_DIR}
                --cpp_out=${PROTO_GEN_DIR} "${PROTO_FILE}"
        DEPENDS "${PROTO_FILE}"
        COMMENT "protoc: ${PROTO_REL}"
    )
endforeach()

if(PROTO_GEN_SOURCES)
    add_library(someip_proto STATIC ${PROTO_GEN_SOURCES})
    target_include_directories(someip_proto PUBLIC "${PROTO_GEN_DIR}")
    target_link_libraries(someip_proto PUBLIC PkgConfig::PROTOBUF)
    set_target_properties(someip_proto PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

# ============================================================
# SOME/IP 中间件（CR-006 §11.4 HOST/TARGET 分离）
# - HOST：CommonAPI core/SOME-IP generator 仅在受控 HOST 执行，不进入
#   TARGET link / runtime / package（BUILD toolchain 已保证
#   CMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER / LIBRARY|INCLUDE|PACKAGE=ONLY）
# - TARGET：vsomeip / CommonAPI runtime 来自 Orin sysroot 或受控 recipe，
#   BUILD 将 <TBOX_DEP_STAGING>/usr 注入 CMAKE_PREFIX_PATH。
# - PKG_CONFIG_USE_CMAKE_PREFIX_PATH=ON：pkg_check_modules 跟随
#   CMAKE_PREFIX_PATH 解析 dep staging 的 .pc，兼容 BUILD 注入。
# ============================================================
if(NOT USE_MOCK_SOMEIP)
    message(STATUS "Using real vsomeip implementation")
    find_package(PkgConfig REQUIRED)
    set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH ON)
    pkg_check_modules(VSOMEIP REQUIRED IMPORTED_TARGET vsomeip3)
    pkg_check_modules(COMMONAPI REQUIRED IMPORTED_TARGET CommonAPI)
    pkg_check_modules(COMMONAPI_SOMEIP REQUIRED IMPORTED_TARGET CommonAPI-SomeIP)
else()
    # 代码中 #ifdef USE_MOCK_SOMEIP 决定 mock vsomeip 实现与 mock IPC client 注册
    add_definitions(-DUSE_MOCK_SOMEIP)
endif()

# ============================================================
# 测试（可选；HOST/隔离 test profile 使用，不进入量产 package）
# BUILD 模式默认 OFF：BUILD 只构建/安装 someip-runtime，不构建 GTest。
# ============================================================
option(BUILD_TESTS "Build tests" OFF)

add_subdirectory(src)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
