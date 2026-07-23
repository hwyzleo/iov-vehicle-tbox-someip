#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <chrono>
#include <optional>
#include <vector>

namespace tbox {
namespace someip {

// 统一服务寻址（唯一 SSOT：整车 SOME/IP Service Registry）
namespace someip_addressing {
    constexpr uint16_t SERVICE_ID = 0x6101;
    constexpr uint16_t INSTANCE_ID = 0x0001;
    constexpr uint16_t TCP_PORT = 56101;
    constexpr uint16_t SD_PORT = 30490;
}

// Method ID（service-scoped，0x0001–0x7FFF）
namespace method_ids {
    constexpr uint16_t REPORT_SOFTWARE_INVENTORY = 0x0001;  // FOTA 上行
    // 后续 CR 为 TimeSync/Location/NetStatus/RemoteCmdRelay 分配
} // namespace method_ids

// Event ID（service-scoped，0x8000–0xFFFF）
namespace event_ids {
    constexpr uint16_t FOTA_COMMAND = 0x8001;  // FOTA 下行（预留）
    // 后续 CR 为其他能力分配
} // namespace event_ids

// 时间同步数据结构
struct TimeData {
    std::chrono::system_clock::time_point timestamp;
    int64_t utc_seconds;
    int32_t utc_offset;
    bool is_valid;
};

// 位置数据结构
struct PositionData {
    double latitude;      // 纬度（度）
    double longitude;     // 经度（度）
    double altitude;      // 海拔（米）
    double speed;         // 车速（km/h）
    double heading;       // 航向（度）
    bool is_valid;
};

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

// FOTA 命令（预留，待后续 CR 细化）
struct FotaCommand {
    std::string command_id;
    std::string command_type;
    std::string payload;
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
using TimeUpdateCallback = std::function<void(const TimeData&)>;
using PositionCallback = std::function<void(const PositionData&)>;
using NetStatusCallback = std::function<void(const NetStatusData&)>;
using RemoteCommandCallback = std::function<void(const RemoteCommand&)>;
using FotaCommandCallback = std::function<void(const FotaCommand&)>;

// 服务状态
enum class ServiceState {
    UNAVAILABLE,    // 不可用
    INITIALIZING,   // 初始化中
    AVAILABLE,      // 可用
    ERROR           // 错误
};

} // namespace someip
} // namespace tbox
