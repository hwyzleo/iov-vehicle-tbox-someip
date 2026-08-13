#pragma once

// 通用车云消息配置（CR-008）：单一 SSOT 定义，供 config.h（ServiceConfig）、
// GenericMessageBridge、VehicleMessageValidator 与 daemon 共用。
// 运行时从 someip.yaml generic_message.* / bridge.* 读取（main.cpp 回填）。

#include <cstdint>
#include <string>
#include <vector>

namespace tbox {
namespace someip {

// 资源/allowlist 限制（someip.yaml generic_message.*；有界默认由代码/容量测试回填）
struct VehicleMessageLimits {
    std::vector<std::string> allowed_services = {"vehicle.fota"};   // 首期且当前仅 vehicle.fota
    std::vector<uint32_t> allowed_protocol_majors = {1};
    uint32_t max_envelope_bytes = 16384;   // 序列化 Envelope 总长上限
    uint32_t max_payload_bytes = 8192;     // payload(10) 上限
};

// 桥接资源/背压限制（CR-008 §配置增量）
struct GenericMessageConfig {
    VehicleMessageLimits limits;
    uint32_t max_in_flight = 64;            // 并发上行上限
    uint32_t downlink_queue_capacity = 256; // 下行队列上限（满则丢旧保序）
    uint32_t worker_count = 1;              // 下行单源有序 worker
    uint32_t request_deadline_ms = 3000;    // SOME/IP Method deadline
    uint32_t tsp_timeout_ms = 2500;         // 须 < request_deadline_ms
};

} // namespace someip
} // namespace tbox
