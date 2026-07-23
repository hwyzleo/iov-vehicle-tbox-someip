#include "net_status.h"
#include "ipc_bridge.h"
#include <iostream>
#include <chrono>

namespace tbox {
namespace someip {

NetStatusService::NetStatusService(IpcBridge* bridge) 
    : bridge_(bridge)
    , state_(ServiceState::UNAVAILABLE)
    , running_(false) {}

NetStatusService::~NetStatusService() {
    stop();
}

bool NetStatusService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::UNAVAILABLE) {
        std::cerr << "NetStatusService already initialized" << std::endl;
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
    std::cout << "NetStatusService initialized" << std::endl;
    return true;
}

bool NetStatusService::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::INITIALIZING) {
        std::cerr << "NetStatusService not in initializing state" << std::endl;
        return false;
    }
    
    // 启动工作线程
    running_ = true;
    worker_thread_ = std::thread(&NetStatusService::run, this);
    
    state_ = ServiceState::AVAILABLE;
    std::cout << "NetStatusService started" << std::endl;
    return true;
}

void NetStatusService::stop() {
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
    
    status_callbacks_.clear();
    state_ = ServiceState::UNAVAILABLE;
    
    std::cout << "NetStatusService stopped" << std::endl;
}

ServiceState NetStatusService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

uint16_t NetStatusService::getServiceId() const {
    return someip_addressing::SERVICE_ID;
}

uint16_t NetStatusService::getInstanceId() const {
    return someip_addressing::INSTANCE_ID;
}

ErrorCode NetStatusService::getNetStatus(NetStatusData& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::AVAILABLE) {
        return ErrorCode::CLIENT_CALL_FAILED;
    }
    
    if (!tsp_client_ || !tsp_client_->isConnected()) {
        return ErrorCode::CLIENT_CALL_FAILED;
    }
    
    return tsp_client_->getNetStatus(status);
}

void NetStatusService::subscribeNetStatusChanges(NetStatusCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_callbacks_.push_back(callback);
    
    // 订阅网络状态变化
    if (tsp_client_) {
        tsp_client_->subscribeNetStatusChanges([this](const NetStatusData& status) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& cb : status_callbacks_) {
                if (cb) {
                    cb(status);
                }
            }
        });
    }
}

void NetStatusService::run() {
    while (running_) {
        // 定期检查网络状态
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

} // namespace someip
} // namespace tbox
