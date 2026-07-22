#pragma once

#include "tbox_someip/interfaces.h"
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

namespace tbox {
namespace someip {

class IpcBridge;

class TimeSyncService : public ISomeipService {
public:
    TimeSyncService(IpcBridge* bridge);
    ~TimeSyncService() override;
    
    bool initialize() override;
    bool start() override;
    void stop() override;
    ServiceState getState() const override;
    uint16_t getServiceId() const override;
    uint16_t getInstanceId() const override;
    
    // TimeSync 特定方法
    ErrorCode getTime(TimeData& time);
    void subscribeTimeUpdates(TimeUpdateCallback callback);

private:
    void run();
    
    IpcBridge* bridge_;
    ServiceState state_;
    std::shared_ptr<IRtcClient> rtc_client_;
    
    std::vector<TimeUpdateCallback> time_callbacks_;
    mutable std::mutex mutex_;
    
    std::atomic<bool> running_;
    std::thread worker_thread_;
};

} // namespace someip
} // namespace tbox
