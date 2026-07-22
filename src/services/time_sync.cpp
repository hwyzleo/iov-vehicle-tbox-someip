#include "time_sync.h"
#include "ipc_bridge.h"
#include <iostream>
#include <chrono>

namespace tbox {
namespace someip {

TimeSyncService::TimeSyncService(IpcBridge* bridge) 
    : bridge_(bridge)
    , state_(ServiceState::UNAVAILABLE)
    , running_(false) {}

TimeSyncService::~TimeSyncService() {
    stop();
}

bool TimeSyncService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::UNAVAILABLE) {
        std::cerr << "TimeSyncService already initialized" << std::endl;
        return false;
    }
    
    if (!bridge_ || !bridge_->isInitialized()) {
        std::cerr << "IpcBridge not initialized" << std::endl;
        return false;
    }
    
    // 获取 RTC 客户端
    auto client = bridge_->getClient("rtc");
    if (!client) {
        std::cerr << "RTC client not found" << std::endl;
        return false;
    }
    
    rtc_client_ = std::dynamic_pointer_cast<IRtcClient>(client);
    if (!rtc_client_) {
        std::cerr << "Invalid RTC client type" << std::endl;
        return false;
    }
    
    // 初始化 RTC 客户端
    if (!rtc_client_->initialize()) {
        std::cerr << "Failed to initialize RTC client" << std::endl;
        return false;
    }
    
    state_ = ServiceState::INITIALIZING;
    std::cout << "TimeSyncService initialized" << std::endl;
    return true;
}

bool TimeSyncService::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::INITIALIZING) {
        std::cerr << "TimeSyncService not in initializing state" << std::endl;
        return false;
    }
    
    // 启动工作线程
    running_ = true;
    worker_thread_ = std::thread(&TimeSyncService::run, this);
    
    state_ = ServiceState::AVAILABLE;
    std::cout << "TimeSyncService started" << std::endl;
    return true;
}

void TimeSyncService::stop() {
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
    
    if (rtc_client_) {
        rtc_client_->shutdown();
        rtc_client_.reset();
    }
    
    time_callbacks_.clear();
    state_ = ServiceState::UNAVAILABLE;
    
    std::cout << "TimeSyncService stopped" << std::endl;
}

ServiceState TimeSyncService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

uint16_t TimeSyncService::getServiceId() const {
    return service_ids::TIME_SYNC;
}

uint16_t TimeSyncService::getInstanceId() const {
    return instance_ids::DEFAULT;
}

ErrorCode TimeSyncService::getTime(TimeData& time) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::AVAILABLE) {
        return ErrorCode::CLIENT_CALL_FAILED;
    }
    
    if (!rtc_client_ || !rtc_client_->isConnected()) {
        return ErrorCode::CLIENT_CALL_FAILED;
    }
    
    return rtc_client_->getTime(time);
}

void TimeSyncService::subscribeTimeUpdates(TimeUpdateCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    time_callbacks_.push_back(callback);
    
    // 订阅 RTC 时间更新
    if (rtc_client_) {
        rtc_client_->subscribeTimeUpdates([this](const TimeData& time) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& cb : time_callbacks_) {
                if (cb) {
                    cb(time);
                }
            }
        });
    }
}

void TimeSyncService::run() {
    while (running_) {
        // 定期更新时间
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace someip
} // namespace tbox
