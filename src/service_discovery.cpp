#include "service_discovery.h"
#include "tbox_someip/someip_types.h"
#include <iostream>

namespace tbox {
namespace someip {

ServiceDiscovery::ServiceDiscovery() : initialized_(false) {}

ServiceDiscovery::~ServiceDiscovery() {
    shutdown();
}

bool ServiceDiscovery::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        std::cerr << "ServiceDiscovery already initialized" << std::endl;
        return false;
    }
    
    // 创建 SOME/IP 服务
    someip_service_ = createSomeIpService();
    if (!someip_service_) {
        std::cerr << "Failed to create SomeIpService" << std::endl;
        return false;
    }
    
    initialized_ = true;
    std::cout << "ServiceDiscovery initialized successfully" << std::endl;
    return true;
}

bool ServiceDiscovery::offerService(uint16_t service_id, uint16_t instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        std::cerr << "ServiceDiscovery not initialized" << std::endl;
        return false;
    }
    
    // 配置 SOME/IP 服务
    SomeIpServiceConfig config;
    config.service_id = service_id;
    config.instance_id = instance_id;
    config.ip_address = "0.0.0.0";
    config.port = 56101; // 默认端口
    
    // 初始化并启动 SOME/IP 服务
    if (!someip_service_->initialize(config)) {
        std::cerr << "Failed to initialize SomeIpService" << std::endl;
        return false;
    }
    
    if (!someip_service_->start()) {
        std::cerr << "Failed to start SomeIpService" << std::endl;
        return false;
    }
    
    if (!someip_service_->offerService()) {
        std::cerr << "Failed to offer SomeIpService" << std::endl;
        return false;
    }
    
    std::cout << "Offering service: 0x" << std::hex << service_id 
              << " instance: 0x" << instance_id 
              << " on port " << std::dec << someip_service_->getBoundPort() << std::endl;
    return true;
}

void ServiceDiscovery::stopOfferService(uint16_t service_id, uint16_t instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_ || !someip_service_) {
        return;
    }
    
    // 停止发布服务
    someip_service_->stopOfferService();
    someip_service_->stop();
    
    std::cout << "Stopped offering service: 0x" << std::hex << service_id 
              << " instance: 0x" << instance_id << std::endl;
}

void ServiceDiscovery::subscribeServiceAvailability(
    uint16_t service_id,
    uint16_t instance_id,
    std::function<void(bool)> callback) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        std::cerr << "ServiceDiscovery not initialized" << std::endl;
        return;
    }
    
    // 保存回调
    availability_callbacks_[{service_id, instance_id}] = callback;
    
    std::cout << "Subscribed to service availability: 0x" << std::hex << service_id 
              << " instance: 0x" << instance_id << std::endl;
}

void ServiceDiscovery::subscribeClientConnection(
    uint16_t service_id,
    uint16_t instance_id,
    ClientConnectionCallback callback) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        std::cerr << "ServiceDiscovery not initialized" << std::endl;
        return;
    }
    
    // 保存回调
    client_connection_callbacks_[{service_id, instance_id}] = callback;
    
    std::cout << "Subscribed to client connection events: 0x" << std::hex << service_id 
              << " instance: 0x" << instance_id << std::endl;
}

void ServiceDiscovery::simulateClientConnection(
    uint16_t service_id, uint16_t instance_id, uint16_t client_id, bool connected) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        std::cerr << "ServiceDiscovery not initialized" << std::endl;
        return;
    }
    
    auto it = client_connection_callbacks_.find({service_id, instance_id});
    if (it != client_connection_callbacks_.end()) {
        ClientConnectionEvent event;
        event.service_id = service_id;
        event.instance_id = instance_id;
        event.client_id = client_id;
        event.connected = connected;
        event.timestamp = std::chrono::system_clock::now();
        
        // 保存回调，然后在锁外调用
        auto callback = it->second;
        lock.~lock_guard();
        callback(event);
    }
}

void ServiceDiscovery::registerMethodHandler(MethodId method_id, SomeIpRequestHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_ || !someip_service_) {
        std::cerr << "ServiceDiscovery not initialized or SomeIpService not available" << std::endl;
        return;
    }
    
    someip_service_->registerMethodHandler(method_id, std::move(handler));
    std::cout << "Registered method handler for method_id: 0x" << std::hex << method_id << std::endl;
}

void ServiceDiscovery::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return;
    }
    
    // 停止 SOME/IP 服务
    if (someip_service_) {
        someip_service_->stopOfferService();
        someip_service_->stop();
        someip_service_.reset();
    }
    
    availability_callbacks_.clear();
    client_connection_callbacks_.clear();
    initialized_ = false;
    
    std::cout << "ServiceDiscovery shutdown" << std::endl;
}

} // namespace someip
} // namespace tbox
