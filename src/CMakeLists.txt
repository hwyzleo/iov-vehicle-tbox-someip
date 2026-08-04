# ============================================================
# tbox-someip daemon（CR-006：someip-runtime，staging: rootfs）
# ============================================================

# 源文件列表
set(SOURCES
    main.cpp
    someip_daemon.cpp
    service_discovery.cpp
    someip_service.cpp
    ipc_bridge.cpp
    someip_log_adapter.cpp
    someip_context.cpp
    services/time_sync.cpp
    services/location.cpp
    services/net_status.cpp
    services/remote_cmd_relay.cpp
    services/fota_relay.cpp
)

if(USE_REAL_IPC)
    list(APPEND SOURCES bridge/tsp_client_adapter.cpp)
endif()

# 创建可执行文件
add_executable(tbox-someip ${SOURCES})
set_target_properties(tbox-someip PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

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
    target_link_libraries(tbox-someip
        PRIVATE
            PkgConfig::VSOMEIP
            PkgConfig::COMMONAPI
            PkgConfig::COMMONAPI_SOMEIP
    )
endif()

# TBOX framework 组件（config/log/ipc）
target_link_libraries(tbox-someip PRIVATE pthread ${SOMEIP_FRAMEWORK_LIBS})

if(USE_REAL_IPC)
    target_include_directories(tbox-someip PRIVATE ${CMAKE_SOURCE_DIR}/src/bridge)
    # 冻结 release-set SDK（CR-006 §11.3：installed package targets only）
    target_link_libraries(tbox-someip PRIVATE tbox::tsp_client tbox::prov_client)
endif()

# ============================================================
# Install — someip-runtime（CR-006 §8：staging rootfs，逻辑 prefix /usr）
# rootfs 仅安装 daemon / systemd unit / 默认非秘密配置；
# 不安装 HOST generator、测试 peer、fixture、源码、开发头文件、
# 静态库或 CMake package。
# ============================================================

# --- someip-runtime: daemon binary ---
install(TARGETS tbox-someip
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT someip-runtime
)

# --- someip-runtime: systemd unit ---
install(FILES ${CMAKE_SOURCE_DIR}/systemd/tbox-someip.service
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system
    COMPONENT someip-runtime
)

# --- someip-runtime: 默认非秘密配置 ---
# someip.yaml：daemon 服务开关/日志/IPC 桥接参数（无 VIN/SN/ECU UID 实例值、
#   凭据、原始 payload 或生产车辆网络参数；协议常量 0x6101/0x0001/TCP 56101/
#   SD UDP 30490 属非秘密协议配置，可保留）
install(FILES ${CMAKE_SOURCE_DIR}/config/someip.yaml
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/tbox/conf.d
    COMPONENT someip-runtime
)
# someip.json：vsomeip runtime 非秘密配置（服务/路由/SD 参数，供真实
#   vsomeip 模式加载；mock 模式不消费，随 someip-runtime 一并安装）
install(FILES ${CMAKE_SOURCE_DIR}/config/someip.json
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/tbox/someip
    COMPONENT someip-runtime
)
