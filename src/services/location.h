#pragma once

#include "tbox_someip/interfaces.h"
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

namespace tbox {
namespace someip {

class IpcBridge;

class LocationService : public ISomeipService {
public:
    LocationService(IpcBridge* bridge);
    ~LocationService() override;
    
    bool initialize() override;
    bool start() override;
    void stop() override;
    ServiceState getState() const override;
    uint16_t getServiceId() const override;
    uint16_t getInstanceId() const override;
    
    // Location 特定方法
    ErrorCode getPosition(PositionData& position);
    void subscribePositionUpdates(PositionCallback callback);

private:
    void run();
    
    IpcBridge* bridge_;
    ServiceState state_;
    std::shared_ptr<IGnssClient> gnss_client_;
    
    std::vector<PositionCallback> position_callbacks_;
    mutable std::mutex mutex_;
    
    std::atomic<bool> running_;
    std::thread worker_thread_;
};

} // namespace someip
} // namespace tbox
