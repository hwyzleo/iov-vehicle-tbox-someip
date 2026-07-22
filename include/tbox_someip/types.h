#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <chrono>

namespace tbox {
namespace someip {

// 服务 ID 定义（草案，最终以整车服务矩阵标定为准）
namespace service_ids {
    constexpr uint16_t TIME_SYNC = 0x0100;
    constexpr uint16_t LOCATION = 0x0101;
    constexpr uint16_t NET_STATUS = 0x0102;
    constexpr uint16_t REMOTE_CMD_RELAY = 0x0103;
} // namespace service_ids

// 实例 ID 定义
namespace instance_ids {
    constexpr uint16_t DEFAULT = 0x0001;
} // namespace instance_ids

// 方法 ID 定义
namespace method_ids {
    // TimeSync
    constexpr uint16_t GET_TIME = 0x0001;
    
    // Location
    constexpr uint16_t GET_POSITION = 0x0001;
    
    // NetStatus
    constexpr uint16_t GET_NET_STATUS = 0x0001;
    
    // RemoteCmdRelay
    constexpr uint16_t ON_REMOTE_COMMAND = 0x0001;
} // namespace method_ids

// 事件 ID 定义
namespace event_ids {
    // TimeSync
    constexpr uint16_t ON_TIME_UPDATE = 0x8001;
    
    // Location
    constexpr uint16_t ON_POSITION = 0x8001;
    
    // NetStatus
    constexpr uint16_t ON_NET_STATUS_CHANGE = 0x8001;
    
    // RemoteCmdRelay
    constexpr uint16_t ON_REMOTE_COMMAND_NOTIFY = 0x8001;
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

// 服务状态
enum class ServiceState {
    UNAVAILABLE,    // 不可用
    INITIALIZING,   // 初始化中
    AVAILABLE,      // 可用
    ERROR           // 错误
};

} // namespace someip
} // namespace tbox
