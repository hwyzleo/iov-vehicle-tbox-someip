#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <chrono>
#include <optional>
#include <vector>

namespace tbox {
namespace someip {

// 寻址基线默认值（CR-007 方案 C）：运行时 SSOT 为 someip.yaml（代码从 config 读取），
// 此处常量作为 ServiceConfig 字段默认值 + CI 一致性校验锚点 + 测试便利。
// 值 = 整车 SOME/IP Service Registry 批准基线，不经 CR 不得变更。
namespace someip_addressing {
    constexpr uint16_t SERVICE_ID = 0x6101;
    constexpr uint16_t INSTANCE_ID = 0x0001;
    constexpr uint16_t TCP_PORT = 56101;
    constexpr uint16_t SD_PORT = 30490;
}

// Method ID（service-scoped，0x0001–0x7FFF）。基线默认值，运行时以 someip.yaml 为 SSOT。
namespace method_ids {
    constexpr uint16_t REPORT_SOFTWARE_INVENTORY = 0x0001;  // FOTA 上行
    // 后续 CR 为 NetStatus/RemoteCmdRelay 分配
} // namespace method_ids

// Event ID（service-scoped，0x8000–0xFFFF）。基线默认值，运行时以 someip.yaml 为 SSOT。
namespace event_ids {
    constexpr uint16_t FOTA_COMMAND = 0x8001;  // FOTA 下行（预留）
    // 后续 CR 为其他能力分配
} // namespace event_ids

// 网络状态数据结构
struct NetStatusData {
    bool is_online;
    int32_t signal_quality;  // 信号质量 (0-100)
    std::string network_type; // 网络类型 (4G/5G/WiFi)
    bool is_valid;
};

// 远程命令数据结构
struct RemoteCommand {
    std::string command_id;
    std::string command_type;
    std::string payload;
    std::chrono::system_clock::time_point timestamp;
};

// 软件版本快照（复用 CGW-FOTA VehicleSoftwareSnapshot）
struct EcuVersionEntry {
    std::string ecu_id;
    std::optional<std::string> part_number;
    std::optional<std::string> sw_version;
    std::optional<std::string> hw_version;
    std::optional<std::string> error_code;
};

struct VehicleSoftwareSnapshot {
    std::string vin;
    std::string registry_version;
    std::string collected_at;
    uint64_t snapshot_seq;
    std::vector<EcuVersionEntry> ecu_list;
};

// FOTA 命令（下行，来源 tsp_client）
struct FotaCommand {
    std::string command_id;
    std::string delivery_id;   // 投递幂等键（断线重放判定）
    std::string command_type;
    std::string payload;
    std::string trace_id;
    std::string request_id;
};

// 错误码定义
enum class ErrorCode : uint16_t {
    SUCCESS = 0,
    NETWORK_NOT_READY = 1001,      // 网络/端口未就绪，SD 发布失败
    CLIENT_CALL_TIMEOUT = 1002,    // 内部 client 调用超时
    CLIENT_CALL_FAILED = 1003,     // 内部 client 调用失败/不可用
    SERIALIZATION_FAILED = 1004,   // 序列化/映射转换失败
};

// 桥接调用结果语义 (CR-005 §4.2)
// 请求已发送但响应丢失时为 UNKNOWN，不得返回伪成功或空值。
enum class BridgeOutcome : uint8_t {
    SUCCESS = 0,   // 调用成功
    UNKNOWN,       // 请求已发送但响应丢失，结果不确定
    FAILED         // 明确失败
};

// 内部 client 桥接调用结果
struct BridgeCallResult {
    BridgeOutcome outcome = BridgeOutcome::FAILED;
    int32_t error_code = 0;     // 域业务错误码（如 TBOX-TSP-10xx），SUCCESS 时为 0
    std::string msg_id;         // 幂等关联键，用于 UNKNOWN 时查询/单次重试
};

// 回调类型定义
using NetStatusCallback = std::function<void(const NetStatusData&)>;
using RemoteCommandCallback = std::function<void(const RemoteCommand&)>;
using FotaCommandCallback = std::function<void(const FotaCommand&)>;

// 客户端连接事件
struct ClientConnectionEvent {
    uint16_t service_id;
    uint16_t instance_id;
    uint16_t client_id;
    bool connected;  // true=连接, false=断开
    std::chrono::system_clock::time_point timestamp;
};
using ClientConnectionCallback = std::function<void(const ClientConnectionEvent&)>;

// 服务状态
enum class ServiceState {
    UNAVAILABLE,    // 不可用
    INITIALIZING,   // 初始化中
    AVAILABLE,      // 可用
    ERROR           // 错误
};

} // namespace someip
} // namespace tbox
