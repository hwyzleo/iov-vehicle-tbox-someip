#pragma once

#include "tbox_someip/interfaces.h"
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

namespace tbox {
namespace someip {

class IpcBridge;

class FotaRelayService : public ISomeipService {
public:
    FotaRelayService(IpcBridge* bridge);
    ~FotaRelayService() override;
    
    bool initialize() override;
    bool start() override;
    void stop() override;
    ServiceState getState() const override;
    uint16_t getServiceId() const override;
    uint16_t getInstanceId() const override;
    
    // FOTA 上行
    ErrorCode reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot);
    
    // FOTA 下行（预留）
    void subscribeFotaCommands(FotaCommandCallback callback);
    void notifyFotaCommand(const FotaCommand& command);

private:
    void run();
    
    IpcBridge* bridge_;
    ServiceState state_;
    std::shared_ptr<ITspClient> tsp_client_;
    
    std::vector<FotaCommandCallback> fota_callbacks_;
    mutable std::mutex mutex_;
    
    std::atomic<bool> running_;
    std::thread worker_thread_;
};

} // namespace someip
} // namespace tbox
