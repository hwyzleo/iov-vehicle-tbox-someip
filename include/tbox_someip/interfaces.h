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

// GNSS Client 接口
class IGnssClient : public IIpcClient {
public:
    // 获取当前位置
    virtual ErrorCode getPosition(PositionData& position) = 0;
    
    // 订阅位置更新
    virtual void subscribePositionUpdates(PositionCallback callback) = 0;
};

// RTC Client 接口
class IRtcClient : public IIpcClient {
public:
    // 获取当前时间
    virtual ErrorCode getTime(TimeData& time) = 0;
    
    // 订阅时间更新
    virtual void subscribeTimeUpdates(TimeUpdateCallback callback) = 0;
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
    
    // FOTA 上行：上报软件版本快照
    // 返回桥接结果：SUCCESS/UNKNOWN/FAILED，UNKNOWN 时仅可复用 msg_id 查询/单次重试
    virtual BridgeCallResult reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot) = 0;
    
    // FOTA 下行：订阅 FOTA 命令（预留）
    virtual void subscribeFotaCommands(FotaCommandCallback callback) = 0;
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
