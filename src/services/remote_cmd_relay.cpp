#include "remote_cmd_relay.h"
#include "ipc_bridge.h"
#include <iostream>
#include <chrono>

namespace tbox {
namespace someip {

RemoteCmdRelayService::RemoteCmdRelayService(IpcBridge* bridge) 
    : bridge_(bridge)
    , state_(ServiceState::UNAVAILABLE)
    , running_(false) {}

RemoteCmdRelayService::~RemoteCmdRelayService() {
    stop();
}

bool RemoteCmdRelayService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::UNAVAILABLE) {
        std::cerr << "RemoteCmdRelayService already initialized" << std::endl;
        return false;
    }
    
    if (!bridge_ || !bridge_->isInitialized()) {
        std::cerr << "IpcBridge not initialized" << std::endl;
        return false;
    }
    
    // 获取 TSP 客户端
    auto client = bridge_->getClient("tsp");
    if (!client) {
        std::cerr << "TSP client not found" << std::endl;
        return false;
    }
    
    tsp_client_ = std::dynamic_pointer_cast<ITspClient>(client);
    if (!tsp_client_) {
        std::cerr << "Invalid TSP client type" << std::endl;
        return false;
    }
    
    // 初始化 TSP 客户端
    if (!tsp_client_->initialize()) {
        std::cerr << "Failed to initialize TSP client" << std::endl;
        return false;
    }
    
    state_ = ServiceState::INITIALIZING;
    std::cout << "RemoteCmdRelayService initialized" << std::endl;
    return true;
}

bool RemoteCmdRelayService::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::INITIALIZING) {
        std::cerr << "RemoteCmdRelayService not in initializing state" << std::endl;
        return false;
    }
    
    // 订阅远程命令
    if (tsp_client_) {
        tsp_client_->subscribeRemoteCommands([this](const RemoteCommand& command) {
            notifyRemoteCommand(command);
        });
    }
    
    // 启动工作线程
    running_ = true;
    worker_thread_ = std::thread(&RemoteCmdRelayService::run, this);
    
    state_ = ServiceState::AVAILABLE;
    std::cout << "RemoteCmdRelayService started" << std::endl;
    return true;
}

void RemoteCmdRelayService::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ == ServiceState::UNAVAILABLE) {
            return;
        }
        
        running_ = false;
    }
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (tsp_client_) {
        tsp_client_->shutdown();
        tsp_client_.reset();
    }
    
    command_callbacks_.clear();
    state_ = ServiceState::UNAVAILABLE;
    
    std::cout << "RemoteCmdRelayService stopped" << std::endl;
}

ServiceState RemoteCmdRelayService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

uint16_t RemoteCmdRelayService::getServiceId() const {
    return someip_addressing::SERVICE_ID;
}

uint16_t RemoteCmdRelayService::getInstanceId() const {
    return someip_addressing::INSTANCE_ID;
}

void RemoteCmdRelayService::subscribeRemoteCommands(RemoteCommandCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    command_callbacks_.push_back(callback);
}

void RemoteCmdRelayService::notifyRemoteCommand(const RemoteCommand& command) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& callback : command_callbacks_) {
        if (callback) {
            callback(command);
        }
    }
}

void RemoteCmdRelayService::run() {
    while (running_) {
        // 处理远程命令
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace someip
} // namespace tbox
