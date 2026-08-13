#pragma once

#include <string>
#include <cstdint>
#include <optional>
#include "tbox_someip/types.h"   // someip_addressing / reserved_ids 基线默认值
#include "tbox_someip/vehicle_message.h"  // GenericMessageConfig

namespace tbox {
namespace someip {

// 内部 client facade 连接配置（CR-005/CR-006/CR-007）
struct ClientConnConfig {
    std::string socket_path;
    uint32_t connect_timeout_ms = 3000;
};

// 服务配置（CR-007 方案 C / CR-008）
// 寻址/桥接/client 参数从 someip.yaml 运行时读取（config 为运行时 SSOT）；
// types.h 常量为基线默认值 + CI 一致性校验锚点，config 缺失字段时 fallback 到默认值。
struct ServiceConfig {
    // 寻址（运行时 SSOT = someip.yaml；默认值 = Registry 基线）
    uint16_t service_id = someip_addressing::SERVICE_ID;
    uint16_t instance_id = someip_addressing::INSTANCE_ID;
    std::string transport = "TCP";
    uint16_t port = someip_addressing::TCP_PORT;

    // 服务发现
    std::string sd_protocol = "UDP";
    uint16_t sd_port = someip_addressing::SD_PORT;
    uint32_t sd_initial_delay_ms = 10;
    uint32_t sd_repetition_base_delay_ms = 200;

    // 通用车云消息成员（CR-008）：精确 Method/Event ID 由 Registry 分配（US-011）。
    // 未分配前为 nullopt —— daemon 不注册/延迟 Offer 通用成员，不猜测未批准数值。
    std::optional<uint16_t> method_exchange_vehicle_message_id;  // METHOD_EXCHANGE_VEHICLE_MESSAGE
    std::optional<uint16_t> event_vehicle_message_id;            // EVENT_VEHICLE_MESSAGE
    uint32_t eventgroup_id = 1;         // 精确值由 Registry 分配
    uint32_t interface_major = 1;

    // 通用车云消息限制（CR-008：someip.yaml generic_message.*）
    GenericMessageConfig generic_message;

    // 桥接（CR-005/CR-006/CR-007/CR-008）
    uint32_t bridge_request_deadline_ms = 3000;
    uint32_t bridge_queue_capacity = 256;
    uint32_t bridge_worker_count = 1;

    // 内部 client facade 连接
    ClientConnConfig tsp{"/tmp/tbox-tsp.sock", 3000};
    ClientConnConfig prov{"/tmp/tbox-prov.sock", 3000};

    // 服务开关（默认 true；仅 dev profile 配置，production schema 拒绝 CR-007 §8 REQ-008）
    bool enable_net_status = true;
    bool enable_remote_cmd_relay = true;
    bool enable_vehicle_message_gateway = true;
};

// 默认配置
inline ServiceConfig getDefaultConfig() {
    return ServiceConfig{};
}

} // namespace someip
} // namespace tbox
