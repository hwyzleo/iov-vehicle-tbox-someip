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

# 源文件列表
set(SERVICE_SOURCES
    ${CMAKE_SOURCE_DIR}/src/someip_daemon.cpp
    ${CMAKE_SOURCE_DIR}/src/service_discovery.cpp
    ${CMAKE_SOURCE_DIR}/src/someip_service.cpp
    ${CMAKE_SOURCE_DIR}/src/ipc_bridge.cpp
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
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_fota_relay
    PRIVATE
        GTest::gtest
        GTest::gtest_main
        pthread
)

# 设置 rpath
set_target_properties(test_fota_relay PROPERTIES
    BUILD_RPATH "${GTEST_LIB_DIR};${GTEST_MAIN_LIB_DIR}"
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;${GTEST_LIB_DIR};${GTEST_MAIN_LIB_DIR}"
    MACOSX_RPATH ON
)

if(NOT USE_MOCK_SOMEIP)
    target_include_directories(test_fota_relay
        PRIVATE
            ${VSOMEIP_INCLUDE_DIRS}
            ${COMMONAPI_INCLUDE_DIRS}
            ${COMMONAPI_SOMEIP_INCLUDE_DIRS}
    )
    target_link_libraries(test_fota_relay
        PRIVATE
            ${VSOMEIP_LIBRARIES}
            ${COMMONAPI_LIBRARIES}
            ${COMMONAPI_SOMEIP_LIBRARIES}
    )
endif()

# 集成测试
add_executable(test_someip_integration integration/test_someip_integration.cpp ${SERVICE_SOURCES})
target_include_directories(test_someip_integration
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_someip_integration
    PRIVATE
        GTest::gtest
        GTest::gtest_main
        pthread
)

# 设置 rpath
set_target_properties(test_someip_integration PROPERTIES
    BUILD_RPATH "${GTEST_LIB_DIR};${GTEST_MAIN_LIB_DIR}"
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;${GTEST_LIB_DIR};${GTEST_MAIN_LIB_DIR}"
    MACOSX_RPATH ON
)

if(NOT USE_MOCK_SOMEIP)
    target_include_directories(test_someip_integration
        PRIVATE
            ${VSOMEIP_INCLUDE_DIRS}
            ${COMMONAPI_INCLUDE_DIRS}
            ${COMMONAPI_SOMEIP_INCLUDE_DIRS}
    )
    target_link_libraries(test_someip_integration
        PRIVATE
            ${VSOMEIP_LIBRARIES}
            ${COMMONAPI_LIBRARIES}
            ${COMMONAPI_SOMEIP_LIBRARIES}
    )
endif()

# FOTA Handler 测试
add_executable(test_fota_handler unit/test_fota_handler.cpp ${SERVICE_SOURCES})
target_include_directories(test_fota_handler
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_fota_handler
    PRIVATE
        GTest::gtest
        GTest::gtest_main
        pthread
)

# 设置 rpath
set_target_properties(test_fota_handler PROPERTIES
    BUILD_RPATH "${GTEST_LIB_DIR};${GTEST_MAIN_LIB_DIR}"
    INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib;${GTEST_LIB_DIR};${GTEST_MAIN_LIB_DIR}"
    MACOSX_RPATH ON
)

if(NOT USE_MOCK_SOMEIP)
    target_include_directories(test_fota_handler
        PRIVATE
            ${VSOMEIP_INCLUDE_DIRS}
            ${COMMONAPI_INCLUDE_DIRS}
            ${COMMONAPI_SOMEIP_INCLUDE_DIRS}
    )
    target_link_libraries(test_fota_handler
        PRIVATE
            ${VSOMEIP_LIBRARIES}
            ${COMMONAPI_LIBRARIES}
            ${COMMONAPI_SOMEIP_LIBRARIES}
    )
endif()

# 注册测试
add_test(NAME FotaRelayTests COMMAND test_fota_relay)
add_test(NAME SomeipIntegrationTests COMMAND test_someip_integration)
add_test(NAME FotaHandlerTests COMMAND test_fota_handler)
