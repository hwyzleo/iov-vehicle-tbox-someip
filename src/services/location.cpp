#include "location.h"
#include "ipc_bridge.h"
#include <iostream>
#include <chrono>

namespace tbox {
namespace someip {

LocationService::LocationService(IpcBridge* bridge) 
    : bridge_(bridge)
    , state_(ServiceState::UNAVAILABLE)
    , running_(false) {}

LocationService::~LocationService() {
    stop();
}

bool LocationService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::UNAVAILABLE) {
        std::cerr << "LocationService already initialized" << std::endl;
        return false;
    }
    
    if (!bridge_ || !bridge_->isInitialized()) {
        std::cerr << "IpcBridge not initialized" << std::endl;
        return false;
    }
    
    // 获取 GNSS 客户端
    auto client = bridge_->getClient("gnss");
    if (!client) {
        std::cerr << "GNSS client not found" << std::endl;
        return false;
    }
    
    gnss_client_ = std::dynamic_pointer_cast<IGnssClient>(client);
    if (!gnss_client_) {
        std::cerr << "Invalid GNSS client type" << std::endl;
        return false;
    }
    
    // 初始化 GNSS 客户端
    if (!gnss_client_->initialize()) {
        std::cerr << "Failed to initialize GNSS client" << std::endl;
        return false;
    }
    
    state_ = ServiceState::INITIALIZING;
    std::cout << "LocationService initialized" << std::endl;
    return true;
}

bool LocationService::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::INITIALIZING) {
        std::cerr << "LocationService not in initializing state" << std::endl;
        return false;
    }
    
    // 启动工作线程
    running_ = true;
    worker_thread_ = std::thread(&LocationService::run, this);
    
    state_ = ServiceState::AVAILABLE;
    std::cout << "LocationService started" << std::endl;
    return true;
}

void LocationService::stop() {
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
    
    if (gnss_client_) {
        gnss_client_->shutdown();
        gnss_client_.reset();
    }
    
    position_callbacks_.clear();
    state_ = ServiceState::UNAVAILABLE;
    
    std::cout << "LocationService stopped" << std::endl;
}

ServiceState LocationService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

uint16_t LocationService::getServiceId() const {
    return someip_addressing::SERVICE_ID;
}

uint16_t LocationService::getInstanceId() const {
    return someip_addressing::INSTANCE_ID;
}

ErrorCode LocationService::getPosition(PositionData& position) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::AVAILABLE) {
        return ErrorCode::CLIENT_CALL_FAILED;
    }
    
    if (!gnss_client_ || !gnss_client_->isConnected()) {
        return ErrorCode::CLIENT_CALL_FAILED;
    }
    
    return gnss_client_->getPosition(position);
}

void LocationService::subscribePositionUpdates(PositionCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    position_callbacks_.push_back(callback);
    
    // 订阅 GNSS 位置更新
    if (gnss_client_) {
        gnss_client_->subscribePositionUpdates([this](const PositionData& position) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& cb : position_callbacks_) {
                if (cb) {
                    cb(position);
                }
            }
        });
    }
}

void LocationService::run() {
    while (running_) {
        // 定期更新位置
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace someip
} // namespace tbox
