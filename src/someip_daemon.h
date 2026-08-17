#pragma once

#include "tbox_someip/config.h"
#include "ipc_bridge.h"
#include "service_discovery.h"
#include "services/net_status.h"
#include "services/remote_cmd_relay.h"
#include "services/vehicle_message_gateway.h"

#include <memory>
#include <atomic>
#include <thread>
#include <signal.h>

namespace tbox {
namespace someip {

class SomeipDaemon {
public:
    SomeipDaemon();
    ~SomeipDaemon();
    
    // 初始化守护进程
    bool initialize(const ServiceConfig& config = getDefaultConfig());
    
    // 启动守护进程
    bool start();
    
    // 关闭守护进程
    void shutdown();
    
    // 获取运行状态
    bool isRunning() const;

    // 诊断/测试钩子：只读访问 ServiceDiscovery（注册状态查询）
    ServiceDiscovery* serviceDiscovery() const { return service_discovery_.get(); }
    


private:
    // 信号处理
    static void signalHandler(int signum);
    
    // 初始化 IPC 客户端
    bool initializeIpcClients();
    
    // 初始化服务
    bool initializeServices();
    
    // 启动服务发现
    bool startServiceDiscovery();
    
    // 处理客户端连接事件
    void handleClientConnection(const ClientConnectionEvent& event);
    
    ServiceConfig config_;
    
    std::unique_ptr<IpcBridge> ipc_bridge_;
    std::unique_ptr<ServiceDiscovery> service_discovery_;
    
    std::unique_ptr<NetStatusService> net_status_service_;
    std::unique_ptr<RemoteCmdRelayService> remote_cmd_relay_service_;
    std::unique_ptr<VehicleMessageGateway> vehicle_message_gateway_;
    
    std::atomic<bool> running_;
    static SomeipDaemon* instance_;
};

} // namespace someip
} // namespace tbox
