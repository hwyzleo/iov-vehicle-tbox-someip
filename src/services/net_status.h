#pragma once

#include "tbox_someip/interfaces.h"
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

namespace tbox {
namespace someip {

class IpcBridge;

class NetStatusService : public ISomeipService {
public:
    NetStatusService(IpcBridge* bridge, uint16_t service_id, uint16_t instance_id);
    ~NetStatusService() override;
    
    bool initialize() override;
    bool start() override;
    void stop() override;
    ServiceState getState() const override;
    uint16_t getServiceId() const override;
    uint16_t getInstanceId() const override;
    
    // NetStatus 特定方法
    ErrorCode getNetStatus(NetStatusData& status);
    void subscribeNetStatusChanges(NetStatusCallback callback);

private:
    void run();
    
    IpcBridge* bridge_;
    ServiceState state_;
    std::shared_ptr<ITspClient> tsp_client_;
    
    std::vector<NetStatusCallback> status_callbacks_;
    mutable std::mutex mutex_;
    
    std::atomic<bool> running_;
    std::thread worker_thread_;

    uint16_t service_id_;
    uint16_t instance_id_;
};

} // namespace someip
} // namespace tbox
