#include "someip_daemon.h"
#include <iostream>
#include <csignal>

namespace tbox {
namespace someip {

SomeipDaemon* SomeipDaemon::instance_ = nullptr;

SomeipDaemon::SomeipDaemon() : running_(false) {
    instance_ = this;
}

SomeipDaemon::~SomeipDaemon() {
    shutdown();
    instance_ = nullptr;
}

bool SomeipDaemon::initialize(const ServiceConfig& config) {
    config_ = config;
    
    std::cout << "Initializing TBOX-SOMEIP daemon..." << std::endl;
    
    // 设置信号处理
    signal(SIGINT, SomeipDaemon::signalHandler);
    signal(SIGTERM, SomeipDaemon::signalHandler);
    
    // 初始化 IPC 桥接层
    ipc_bridge_ = std::make_unique<IpcBridge>();
    if (!ipc_bridge_->initialize()) {
        std::cerr << "Failed to initialize IPC bridge" << std::endl;
        return false;
    }
    
    // 初始化 IPC 客户端
    if (!initializeIpcClients()) {
        std::cerr << "Failed to initialize IPC clients" << std::endl;
        return false;
    }
    
    // 初始化服务
    if (!initializeServices()) {
        std::cerr << "Failed to initialize services" << std::endl;
        return false;
    }
    
    // 初始化服务发现
    service_discovery_ = std::make_unique<ServiceDiscovery>();
    if (!service_discovery_->initialize()) {
        std::cerr << "Failed to initialize service discovery" << std::endl;
        return false;
    }
    
    std::cout << "TBOX-SOMEIP daemon initialized successfully" << std::endl;
    return true;
}

bool SomeipDaemon::start() {
    if (!ipc_bridge_ || !ipc_bridge_->isInitialized()) {
        std::cerr << "Daemon not initialized" << std::endl;
        return false;
    }
    
    std::cout << "Starting TBOX-SOMEIP daemon..." << std::endl;
    
    // 启动服务发现
    if (!startServiceDiscovery()) {
        std::cerr << "Failed to start service discovery" << std::endl;
        return false;
    }
    
    // 启动所有服务
    if (time_sync_service_) {
        if (!time_sync_service_->start()) {
            std::cerr << "Failed to start TimeSync service" << std::endl;
            return false;
        }
    }
    
    if (location_service_) {
        if (!location_service_->start()) {
            std::cerr << "Failed to start Location service" << std::endl;
            return false;
        }
    }
    
    if (net_status_service_) {
        if (!net_status_service_->start()) {
            std::cerr << "Failed to start NetStatus service" << std::endl;
            return false;
        }
    }
    
    if (remote_cmd_relay_service_) {
        if (!remote_cmd_relay_service_->start()) {
            std::cerr << "Failed to start RemoteCmdRelay service" << std::endl;
            return false;
        }
    }
    
    if (fota_relay_service_) {
        if (!fota_relay_service_->start()) {
            std::cerr << "Failed to start FotaRelay service" << std::endl;
            return false;
        }
    }
    
    running_ = true;
    std::cout << "TBOX-SOMEIP daemon started successfully" << std::endl;
    return true;
}

void SomeipDaemon::shutdown() {
    if (!running_) {
        return;
    }
    
    std::cout << "Shutting down TBOX-SOMEIP daemon..." << std::endl;
    
    running_ = false;
    
    // 停止所有服务
    if (time_sync_service_) {
        time_sync_service_->stop();
    }
    
    if (location_service_) {
        location_service_->stop();
    }
    
    if (net_status_service_) {
        net_status_service_->stop();
    }
    
    if (remote_cmd_relay_service_) {
        remote_cmd_relay_service_->stop();
    }
    
    if (fota_relay_service_) {
        fota_relay_service_->stop();
    }
    
    // 停止服务发现
    if (service_discovery_) {
        service_discovery_->shutdown();
    }
    
    // 关闭 IPC 桥接层
    if (ipc_bridge_) {
        ipc_bridge_->shutdown();
    }
    
    std::cout << "TBOX-SOMEIP daemon shutdown complete" << std::endl;
}

bool SomeipDaemon::isRunning() const {
    return running_;
}

void SomeipDaemon::signalHandler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
    
    if (instance_) {
        instance_->shutdown();
    }
    
    exit(signum);
}

bool SomeipDaemon::initializeIpcClients() {
    // TODO: 实现实际的 IPC 客户端初始化
    // 这里需要创建 GNSS、RTC、TSP、PROV 客户端并注册到 IPC 桥接层
    
    std::cout << "Initializing IPC clients..." << std::endl;
    return true;
}

bool SomeipDaemon::initializeServices() {
    std::cout << "Initializing services..." << std::endl;
    
    // 初始化 TimeSync 服务
    if (config_.enable_time_sync) {
        time_sync_service_ = std::make_unique<TimeSyncService>(ipc_bridge_.get());
        if (!time_sync_service_->initialize()) {
            std::cerr << "Failed to initialize TimeSync service" << std::endl;
            return false;
        }
    }
    
    // 初始化 Location 服务
    if (config_.enable_location) {
        location_service_ = std::make_unique<LocationService>(ipc_bridge_.get());
        if (!location_service_->initialize()) {
            std::cerr << "Failed to initialize Location service" << std::endl;
            return false;
        }
    }
    
    // 初始化 NetStatus 服务
    if (config_.enable_net_status) {
        net_status_service_ = std::make_unique<NetStatusService>(ipc_bridge_.get());
        if (!net_status_service_->initialize()) {
            std::cerr << "Failed to initialize NetStatus service" << std::endl;
            return false;
        }
    }
    
    // 初始化 RemoteCmdRelay 服务
    if (config_.enable_remote_cmd_relay) {
        remote_cmd_relay_service_ = std::make_unique<RemoteCmdRelayService>(ipc_bridge_.get());
        if (!remote_cmd_relay_service_->initialize()) {
            std::cerr << "Failed to initialize RemoteCmdRelay service" << std::endl;
            return false;
        }
    }
    
    // 初始化 FotaRelay 服务
    if (config_.enable_fota_relay) {
        fota_relay_service_ = std::make_unique<FotaRelayService>(ipc_bridge_.get());
        if (!fota_relay_service_->initialize()) {
            std::cerr << "Failed to initialize FotaRelay service" << std::endl;
            return false;
        }
    }
    
    std::cout << "All services initialized" << std::endl;
    return true;
}

bool SomeipDaemon::startServiceDiscovery() {
    if (!service_discovery_) {
        return false;
    }
    
    std::cout << "Starting service discovery..." << std::endl;
    
    // 统一 offer TBOX-SOMEIP 聚合服务
    if (!service_discovery_->offerService(
        someip_addressing::SERVICE_ID,
        someip_addressing::INSTANCE_ID))
    {
        std::cerr << "Failed to offer TBOX-SOMEIP service" << std::endl;
        return false;
    }
    
    std::cout << "Service discovery started" << std::endl;
    return true;
}

} // namespace someip
} // namespace tbox
