#pragma once

#include <string>
#include <cstdint>

namespace tbox {
namespace someip {

// 服务配置
struct ServiceConfig {
    // 网络配置
    std::string network_interface = "eth0";
    uint16_t sd_port = 30490;
    
    // 服务发现配置
    bool enable_sd = true;
    uint32_t sd_offer_interval_ms = 1000;
    uint32_t sd_request_timeout_ms = 5000;
    
    // IPC 配置
    uint32_t ipc_timeout_ms = 1000;

    // IPC 桥接配置 (CR-005)：通过 TBOX 域 client facade 访问内部服务
    std::string tsp_ipc_socket_path = "/tmp/tbox-tsp.sock";
    uint32_t ipc_default_timeout_ms = 3000;
    uint32_t ipc_event_queue_size = 256;
    
    // 日志配置
    std::string log_level = "info";
    std::string log_file = "/var/log/tbox-someip.log";
    
    // 服务实例配置
    bool enable_time_sync = true;
    bool enable_location = true;
    bool enable_net_status = true;
    bool enable_remote_cmd_relay = true;
    bool enable_fota_relay = true;
};

// 默认配置
inline ServiceConfig getDefaultConfig() {
    return ServiceConfig{};
}

} // namespace someip
} // namespace tbox
