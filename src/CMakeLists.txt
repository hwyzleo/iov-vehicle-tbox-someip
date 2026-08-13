# ============================================================
# tbox_someip daemon（CR-006：someip-runtime，staging: rootfs）
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
    services/net_status.cpp
    services/remote_cmd_relay.cpp
    services/vehicle_message_gateway.cpp
    bridge/tsp_client_adapter.cpp
    bridge/prov_client_adapter.cpp
    bridge/vehicle_message_validator.cpp
    bridge/generic_message_bridge.cpp
)

# 创建可执行文件
add_executable(tbox_someip ${SOURCES})
set_target_properties(tbox_someip PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# 包含目录
target_include_directories(tbox_someip
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
)

# 链接库
if(USE_MOCK_SOMEIP)
    # 模拟实现不需要额外链接
else()
    target_link_libraries(tbox_someip
        PRIVATE
            PkgConfig::VSOMEIP
            PkgConfig::COMMONAPI
            PkgConfig::COMMONAPI_SOMEIP
    )
endif()

# TBOX framework 组件（config/log/ipc）
target_link_libraries(tbox_someip PRIVATE pthread ${SOMEIP_FRAMEWORK_LIBS})

# CR-008 通用车云消息：protobuf 生成的 Envelope 代码（HOST 生成，TARGET runtime）
if(TARGET someip_proto)
    target_link_libraries(tbox_someip PRIVATE someip_proto)
endif()

# framework-ipc 域 client facade（TSP/PROV adapter 始终链接）
target_include_directories(tbox_someip PRIVATE ${CMAKE_SOURCE_DIR}/src/bridge)
target_link_libraries(tbox_someip PRIVATE tbox::tsp_client tbox::prov_client)

# ============================================================
# Install — someip-runtime（CR-006 §8：staging rootfs，逻辑 prefix /usr）
# rootfs 仅安装 daemon / systemd unit / 默认非秘密配置；
# 不安装 HOST generator、测试 peer、fixture、源码、开发头文件、
# 静态库或 CMake package。
# ============================================================

# --- someip-runtime: daemon binary ---
install(TARGETS tbox_someip
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    COMPONENT someip-runtime
)

# --- someip-runtime: systemd unit ---
install(FILES ${CMAKE_SOURCE_DIR}/systemd/tbox-someip.service
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/systemd/system
    COMPONENT someip-runtime
)

# --- someip-runtime: 默认非秘密配置（CR-007 §3/§6）---
# someip.default.yaml -> /etc/tbox/conf.d/someip.yaml（RENAME）：
#   量产默认模板，由 someip-runtime component 安装；BUILD 可选 overlay 覆盖。
#   不含 VIN/SN/ECU UID 实例值、凭据、原始 payload 或生产车辆网络参数。
install(FILES ${CMAKE_SOURCE_DIR}/config/someip.default.yaml
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/tbox/conf.d
    RENAME someip.yaml
    COMPONENT someip-runtime
)
# vsomeip.json：vsomeip runtime 非秘密配置（CR-007 §7：明确命名的独立中间件
#   runtime 资产，供真实 vsomeip 模式加载；mock 模式不消费。寻址字段与
#   someip.yaml 一致由 CI gate D 校验，不成为第二套无校验 SSOT）
install(FILES ${CMAKE_SOURCE_DIR}/config/runtime/vsomeip.json
    DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/tbox/someip
    COMPONENT someip-runtime
)
