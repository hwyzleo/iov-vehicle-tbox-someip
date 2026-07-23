# 查找 GTest
find_package(GTest REQUIRED)

# 源文件列表
set(SERVICE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/someip_daemon.cpp
    ${CMAKE_SOURCE_DIR}/src/service_discovery.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc_bridge.cpp
    ${CMAKE_SOURCE_DIR}/src/service_registry.cpp
    ${CMAKE_SOURCE_DIR}/src/services/time_sync.cpp
    ${CMAKE_SOURCE_DIR}/src/services/location.cpp
    ${CMAKE_SOURCE_DIR}/src/services/net_status.cpp
    ${CMAKE_SOURCE_DIR}/src/services/remote_cmd_relay.cpp
    ${CMAKE_SOURCE_DIR}/src/services/fota_relay.cpp
)

# 单元测试
add_executable(test_fota_relay unit/test_fota_relay.cpp ${SERVICE_SOURCES})
target_include_directories(test_fota_relay
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${VSOMEIP_INCLUDE_DIRS}
        ${COMMONAPI_INCLUDE_DIRS}
        ${COMMONAPI_SOMEIP_INCLUDE_DIRS}
)
target_link_libraries(test_fota_relay
    PRIVATE
        GTest::GTest
        GTest::Main
        ${VSOMEIP_LIBRARIES}
        ${COMMONAPI_LIBRARIES}
        ${COMMONAPI_SOMEIP_LIBRARIES}
        pthread
)

# 集成测试
add_executable(test_someip_integration integration/test_someip_integration.cpp ${SERVICE_SOURCES})
target_include_directories(test_someip_integration
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${VSOMEIP_INCLUDE_DIRS}
        ${COMMONAPI_INCLUDE_DIRS}
        ${COMMONAPI_SOMEIP_INCLUDE_DIRS}
)
target_link_libraries(test_someip_integration
    PRIVATE
        GTest::GTest
        GTest::Main
        ${VSOMEIP_LIBRARIES}
        ${COMMONAPI_LIBRARIES}
        ${COMMONAPI_SOMEIP_LIBRARIES}
        pthread
)

# 注册测试
add_test(NAME FotaRelayTests COMMAND test_fota_relay)
add_test(NAME SomeipIntegrationTests COMMAND test_someip_integration)
