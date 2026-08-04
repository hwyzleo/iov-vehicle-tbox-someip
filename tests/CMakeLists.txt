# 查找 GTest
find_package(GTest REQUIRED)

# 从 imported target 获取 GTest 库目录
get_target_property(GTEST_LIB_LOCATION GTest::gtest IMPORTED_LOCATION_RELEASE)
get_target_property(GTEST_MAIN_LIB_LOCATION GTest::gtest_main IMPORTED_LOCATION_RELEASE)

get_filename_component(GTEST_LIB_DIR "${GTEST_LIB_LOCATION}" DIRECTORY)
get_filename_component(GTEST_MAIN_LIB_DIR "${GTEST_MAIN_LIB_LOCATION}" DIRECTORY)

message(STATUS "GTest library: ${GTEST_LIB_LOCATION}")
message(STATUS "GTest main library: ${GTEST_MAIN_LIB_LOCATION}")
message(STATUS "GTest library dir: ${GTEST_LIB_DIR}")
message(STATUS "GTest main library dir: ${GTEST_MAIN_LIB_DIR}")

# 源文件列表（含日志适配器与上下文辅助）
set(SERVICE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/someip_daemon.cpp
    ${CMAKE_SOURCE_DIR}/src/service_discovery.cpp
    ${CMAKE_SOURCE_DIR}/src/someip_service.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/someip_log_adapter.cpp
    ${CMAKE_SOURCE_DIR}/src/someip_context.cpp
    ${CMAKE_SOURCE_DIR}/src/services/time_sync.cpp
    ${CMAKE_SOURCE_DIR}/src/services/location.cpp
    ${CMAKE_SOURCE_DIR}/src/services/net_status.cpp
    ${CMAKE_SOURCE_DIR}/src/services/remote_cmd_relay.cpp
    ${CMAKE_SOURCE_DIR}/src/services/fota_relay.cpp
)

if(USE_REAL_IPC)
    list(APPEND SERVICE_SOURCES ${CMAKE_SOURCE_DIR}/src/bridge/tsp_client_adapter.cpp)
endif()

# 公共测试依赖（使用自定义 test_main.cpp 代替 gtest_main，以便在退出前 flush Logger）
set(TEST_LIBS GTest::gtest pthread ${SOMEIP_FRAMEWORK_LIBS})
set(TEST_INCLUDES ${CMAKE_SOURCE_DIR}/include ${CMAKE_SOURCE_DIR}/src)
set(TEST_RPATH "${GTEST_LIB_DIR};${GTEST_MAIN_LIB_DIR}")

# ============================================================
# 辅助函数：配置测试目标
# ============================================================
function(configure_someip_test target_name)
    target_include_directories(${target_name} PRIVATE ${TEST_INCLUDES})
    target_link_libraries(${target_name} PRIVATE ${TEST_LIBS})
    set_target_properties(${target_name} PROPERTIES
        BUILD_RPATH "${TEST_RPATH}"
        INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;${TEST_RPATH}"
        MACOSX_RPATH ON
    )
    if(NOT USE_MOCK_SOMEIP)
        target_link_libraries(${target_name}
            PRIVATE
                PkgConfig::VSOMEIP
                PkgConfig::COMMONAPI
                PkgConfig::COMMONAPI_SOMEIP
        )
    endif()
    if(USE_REAL_IPC)
        target_include_directories(${target_name} PRIVATE ${CMAKE_SOURCE_DIR}/src/bridge)
        target_link_libraries(${target_name} PRIVATE tbox::tsp_client tbox::prov_client)
    endif()
endfunction()

# ============================================================
# 单元测试
# ============================================================

# FOTA Relay 测试
add_executable(test_fota_relay unit/test_fota_relay.cpp ${CMAKE_SOURCE_DIR}/tests/test_main.cpp ${SERVICE_SOURCES})
configure_someip_test(test_fota_relay)

# FOTA Handler 测试（有自己的 main()）
add_executable(test_fota_handler unit/test_fota_handler.cpp ${SERVICE_SOURCES})
configure_someip_test(test_fota_handler)

# 日志适配器测试
add_executable(test_log_adapter unit/test_log_adapter.cpp ${CMAKE_SOURCE_DIR}/tests/test_main.cpp ${SERVICE_SOURCES})
configure_someip_test(test_log_adapter)

# 日志事件测试
add_executable(test_log_events unit/test_log_events.cpp ${CMAKE_SOURCE_DIR}/tests/test_main.cpp ${SERVICE_SOURCES})
configure_someip_test(test_log_events)

# 上下文传播测试
add_executable(test_someip_context unit/test_someip_context.cpp ${CMAKE_SOURCE_DIR}/tests/test_main.cpp ${SERVICE_SOURCES})
configure_someip_test(test_someip_context)

# ============================================================
# 集成测试
# ============================================================

# SOME/IP 集成测试
add_executable(test_someip_integration integration/test_someip_integration.cpp ${CMAKE_SOURCE_DIR}/tests/test_main.cpp ${SERVICE_SOURCES})
configure_someip_test(test_someip_integration)

# 日志集成测试
add_executable(test_log_integration integration/test_log_integration.cpp ${CMAKE_SOURCE_DIR}/tests/test_main.cpp ${SERVICE_SOURCES})
configure_someip_test(test_log_integration)

# ============================================================
# 注册测试
# ============================================================
add_test(NAME FotaRelayTests COMMAND test_fota_relay)
add_test(NAME FotaHandlerTests COMMAND test_fota_handler)
add_test(NAME LogAdapterTests COMMAND test_log_adapter)
add_test(NAME LogEventsTests COMMAND test_log_events)
add_test(NAME SomeipContextTests COMMAND test_someip_context)
add_test(NAME SomeipIntegrationTests COMMAND test_someip_integration)
add_test(NAME LogIntegrationTests COMMAND test_log_integration)

# ============================================================
# CR-005: TSP client adapter 测试（仅 USE_REAL_IPC）
# ============================================================
if(USE_REAL_IPC)
    add_executable(test_tsp_client_adapter unit/test_tsp_client_adapter.cpp
        ${CMAKE_SOURCE_DIR}/tests/test_main.cpp ${SERVICE_SOURCES})
    configure_someip_test(test_tsp_client_adapter)
    add_test(NAME TspClientAdapterTests COMMAND test_tsp_client_adapter)
endif()
