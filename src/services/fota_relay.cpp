#include "fota_relay.h"
#include "ipc_bridge.h"
#include <iostream>
#include <chrono>

namespace tbox {
namespace someip {

FotaRelayService::FotaRelayService(IpcBridge* bridge) 
    : bridge_(bridge)
    , state_(ServiceState::UNAVAILABLE)
    , running_(false) {}

FotaRelayService::~FotaRelayService() {
    stop();
}

bool FotaRelayService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::UNAVAILABLE) {
        std::cerr << "FotaRelayService already initialized" << std::endl;
        return false;
    }
    
    if (!bridge_ || !bridge_->isInitialized()) {
        std::cerr << "IpcBridge not initialized" << std::endl;
        return false;
    }
    
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
    
    if (!tsp_client_->initialize()) {
        std::cerr << "Failed to initialize TSP client" << std::endl;
        return false;
    }
    
    state_ = ServiceState::INITIALIZING;
    std::cout << "FotaRelayService initialized" << std::endl;
    return true;
}

bool FotaRelayService::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::INITIALIZING) {
        std::cerr << "FotaRelayService not in initializing state" << std::endl;
        return false;
    }
    
    running_ = true;
    worker_thread_ = std::thread(&FotaRelayService::run, this);
    
    state_ = ServiceState::AVAILABLE;
    std::cout << "FotaRelayService started" << std::endl;
    return true;
}

void FotaRelayService::stop() {
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
    
    fota_callbacks_.clear();
    state_ = ServiceState::UNAVAILABLE;
    
    std::cout << "FotaRelayService stopped" << std::endl;
}

ServiceState FotaRelayService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

uint16_t FotaRelayService::getServiceId() const {
    return someip_addressing::SERVICE_ID;
}

uint16_t FotaRelayService::getInstanceId() const {
    return someip_addressing::INSTANCE_ID;
}

ErrorCode FotaRelayService::reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::AVAILABLE) {
        return ErrorCode::CLIENT_CALL_FAILED;
    }
    
    if (!tsp_client_) {
        return ErrorCode::CLIENT_CALL_FAILED;
    }
    
    return tsp_client_->reportSoftwareInventory(snapshot);
}

void FotaRelayService::subscribeFotaCommands(FotaCommandCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    fota_callbacks_.push_back(callback);
    
    // 预留：注册到 tsp_client
    if (tsp_client_) {
        tsp_client_->subscribeFotaCommands([this](const FotaCommand& command) {
            notifyFotaCommand(command);
        });
    }
}

void FotaRelayService::notifyFotaCommand(const FotaCommand& command) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& callback : fota_callbacks_) {
        if (callback) {
            callback(command);
        }
    }
}

void FotaRelayService::run() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace someip
} // namespace tbox
