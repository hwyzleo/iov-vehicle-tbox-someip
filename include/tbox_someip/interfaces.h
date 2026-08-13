#pragma once

#include "types.h"
#include <functional>

namespace tbox {
namespace someip {

// IPC Client 接口
class IIpcClient {
public:
    virtual ~IIpcClient() = default;
    
    // 初始化客户端
    virtual bool initialize() = 0;
    
    // 检查连接状态
    virtual bool isConnected() const = 0;
    
    // 关闭客户端
    virtual void shutdown() = 0;
};

// TSP/MQTT Client 接口
class ITspClient : public IIpcClient {
public:
    // 获取网络状态
    virtual ErrorCode getNetStatus(NetStatusData& status) = 0;
    
    // 订阅网络状态变化
    virtual void subscribeNetStatusChanges(NetStatusCallback callback) = 0;
    
    // 订阅远程命令
    virtual void subscribeRemoteCommands(RemoteCommandCallback callback) = 0;

    // ---- CR-008 通用车云消息契约 ----
    // 通用 exchange/downlink 能力探测（US-013）：
    // 后续版本化 tbox::tsp_client 通用消息 API 未就绪或版本不兼容时返回 false，
    // GenericMessageBridge 进入 DEGRADED，对外返回 VersionMismatch/Unavailable，
    // 不回退解析 OTA payload 或本地拼装云端协议。
    virtual bool supportsVehicleMessage() const = 0;

    // 上行：通用 request/response。SOMEIP 保持 messageId/correlationId/
    // idempotencyKey/trace；不得由 SOMEIP 重新生成业务身份（US-013）。
    virtual VehicleMessageResponseFrame exchangeVehicleMessage(
        const VehicleMessageFrame& frame) = 0;

    // 下行：独立订阅 + 有界队列；慢消费者不得阻塞 TSP callback / SOME/IP
    // I/O / SD 或其他能力（US-013）。
    virtual void subscribeVehicleMessage(std::string_view service,
                                         VehicleMessageHandler handler) = 0;
};

// PROV Client 接口
class IProvClient : public IIpcClient {
public:
    // 获取配置
    virtual std::string getConfiguration(const std::string& key) = 0;
};

// SOME/IP 服务接口
class ISomeipService {
public:
    virtual ~ISomeipService() = default;
    
    // 初始化服务
    virtual bool initialize() = 0;
    
    // 启动服务
    virtual bool start() = 0;
    
    // 停止服务
    virtual void stop() = 0;
    
    // 获取服务状态
    virtual ServiceState getState() const = 0;
    
    // 获取服务 ID
    virtual uint16_t getServiceId() const = 0;
    
    // 获取实例 ID
    virtual uint16_t getInstanceId() const = 0;
};

// 服务发现接口
class IServiceDiscovery {
public:
    virtual ~IServiceDiscovery() = default;
    
    // 初始化服务发现
    virtual bool initialize() = 0;
    
    // 发布服务
    virtual bool offerService(uint16_t service_id, uint16_t instance_id) = 0;
    
    // 停止发布服务
    virtual void stopOfferService(uint16_t service_id, uint16_t instance_id) = 0;
    
    // 订阅服务可用性
    virtual void subscribeServiceAvailability(
        uint16_t service_id,
        uint16_t instance_id,
        std::function<void(bool)> callback
    ) = 0;
    
    // 订阅客户端连接事件
    virtual void subscribeClientConnection(
        uint16_t service_id,
        uint16_t instance_id,
        ClientConnectionCallback callback
    ) = 0;
    
    // 模拟客户端连接（用于测试）
    virtual void simulateClientConnection(uint16_t service_id, uint16_t instance_id, uint16_t client_id, bool connected) = 0;
    
    // 关闭服务发现
    virtual void shutdown() = 0;
};

} // namespace someip
} // namespace tbox
