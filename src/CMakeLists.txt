# 源文件列表
set(SOURCES
    main.cpp
    someip_daemon.cpp
    service_discovery.cpp
    ipc_bridge.cpp
    services/time_sync.cpp
    services/location.cpp
    services/net_status.cpp
    services/remote_cmd_relay.cpp
    services/fota_relay.cpp
)

# 创建可执行文件
add_executable(tbox-someip ${SOURCES})

# 包含目录
target_include_directories(tbox-someip
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

# 链接库
if(USE_MOCK_SOMEIP)
    # 模拟实现不需要额外链接
else()
    target_include_directories(tbox-someip
        PRIVATE
            ${VSOMEIP_INCLUDE_DIRS}
            ${COMMONAPI_INCLUDE_DIRS}
            ${COMMONAPI_SOMEIP_INCLUDE_DIRS}
    )
    target_link_libraries(tbox-someip
        PRIVATE
            ${VSOMEIP_LIBRARIES}
            ${COMMONAPI_LIBRARIES}
            ${COMMONAPI_SOMEIP_LIBRARIES}
    )
endif()

target_link_libraries(tbox-someip PRIVATE pthread)

# 安装
install(TARGETS tbox-someip
    RUNTIME DESTINATION bin
)

install(FILES ${CMAKE_SOURCE_DIR}/systemd/tbox-someip.service
    DESTINATION /etc/systemd/system/
)
