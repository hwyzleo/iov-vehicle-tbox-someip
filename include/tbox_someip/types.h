#pragma once

#include <cstddef>
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

// 旧 FOTA 专用成员 ID（CR-008 §取代与迁移/§3.3）：
// METHOD_REPORT_SOFTWARE_INVENTORY(0x0001) 与 EVENT_FOTA_COMMAND(0x8001) 已删除。
// 0x0001/0x8001 在 Service Registry / IDL / 生成规则中永久 reserved：
// 不得复用、不得重编号、不得改作通用 Envelope 语义、不得双注册（US-017）。
// 本命名空间仅用于保留/拒绝扫描与 CI 一致性校验，代码不得注册或分发这些 ID。
namespace reserved_ids {
    constexpr uint16_t REPORT_SOFTWARE_INVENTORY = 0x0001;  // reserved（旧 FOTA 上行）
    constexpr uint16_t FOTA_COMMAND = 0x8001;               // reserved（旧 FOTA 下行）
}

// 通用车云消息成员语义名（CR-008 §3.3）。
// 精确 Method/Event/Eventgroup ID 与接口版本由整车 SOME/IP Service Registry
// 分配（US-011），本 CR 不猜测/占用未批准数值；运行时以 someip.yaml 为 SSOT。
namespace member_names {
    inline constexpr const char* kExchangeVehicleMessage = "vehicle_message_exchange";
    inline constexpr const char* kVehicleMessageDownlink = "vehicle_message_downlink";
}

// ============================================================
// 通用车云消息（CR-008）——传输层 DTO
// SOMEIP 只消费公共 Envelope（vehicle.common.v1.VehicleMessageEnvelope，
// 见 proto/），payload 为不透明 bytes；SOMEIP 不做业务解析。
// ============================================================

// 传输结果（CR-008 §错误映射 / US-014）：
// Accepted 不等于 MQTT PUBACK 或 FOTA 业务成功；response 仅在云端/TSP 返回
// 对应业务 RESPONSE Envelope 时存在。
enum class TransportOutcome : uint8_t {
    Accepted = 0,        // 已受理并转交 TSP（不表示业务成功）
    Rejected,            // 拒绝（TTL 过期、allowlist/容量拒绝等）
    Timeout,             // TSP/cloud 超时
    Unknown,             // 结果不确定（请求已发出但响应丢失）
    Unavailable,         // TSP client 不可用 / 能力未就绪（DEGRADED）
    VersionMismatch,     // 能力/协议版本不兼容（不走 legacy fallback）
    PayloadTooLarge,     // 超出资源上限（不截断、不分片、不落盘）
    ProtocolError,       // Envelope/IDL/关联错误（映射 TBOX-SOMEIP-1004）
    Stopping             // 服务停止中，拒绝新请求
};

// 传输错误（可选附加信息；不解释 payload 内 OTA 业务错误）
struct TransportError {
    uint16_t code = 0;       // 映射 TBOX-SOMEIP-1001~1004
    std::string summary;     // 受控摘要（不记录完整 Envelope/payload/身份）
};

// SOME/IP wire frame：单一序列化 vehicle.common.v1.VehicleMessageEnvelope
struct VehicleMessageFrame {
    std::vector<std::byte> envelope_bytes;
};

// SOME/IP 通用 Method 响应 frame（CR-008 §2 Wire DTO）
struct VehicleMessageResponseFrame {
    TransportOutcome outcome = TransportOutcome::Unknown;
    std::optional<VehicleMessageFrame> response;   // 仅业务 RESPONSE Envelope 存在时
    std::optional<TransportError> error;
};

// 公共 Envelope 解码视图（SOMEIP 准入/关联/路由用；payload 保持不透明）
struct VehicleMessageEnvelope {
    int64_t request_id = 0;
    int64_t timestamp_ms = 0;
    uint32_t protocol_version = 0;
    std::string device_id;
    std::string vin;
    std::string vehicle_task_id;
    std::string execution_id;
    std::string idempotency_key;
    std::string payload_type;
    std::vector<std::byte> payload;   // opaque bytes
    std::string message_id;
    std::string correlation_id;
    std::string message_kind;
    std::string service;
    int64_t expire_at_ms = 0;
    std::string trace_context;
    size_t serialized_length = 0;     // 原始 envelope 字节长度（准入用）
};

// 下行 Event 回调（onVehicleMessage）：携带原始序列化 Envelope，SOMEIP 直接
// 作为 EVENT_VEHICLE_MESSAGE 通知 payload 下发 CGW。
using VehicleMessageHandler = std::function<void(const VehicleMessageFrame&)>;

// ============================================================
// 既有网络状态 / 远程命令结构（非 CR-008 范围）
// ============================================================

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

// 错误码定义
enum class ErrorCode : uint16_t {
    SUCCESS = 0,
    NETWORK_NOT_READY = 1001,      // 网络/端口未就绪，SD 发布失败
    CLIENT_CALL_TIMEOUT = 1002,    // 内部 client 调用超时
    CLIENT_CALL_FAILED = 1003,     // 内部 client 调用失败/不可用
    SERIALIZATION_FAILED = 1004,   // 序列化/映射转换失败
};

// 回调类型定义
using NetStatusCallback = std::function<void(const NetStatusData&)>;
using RemoteCommandCallback = std::function<void(const RemoteCommand&)>;

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
