#pragma once

#include "tbox_someip/interfaces.h"
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

namespace tbox {
namespace someip {

class IpcBridge;

class RemoteCmdRelayService : public ISomeipService {
public:
    RemoteCmdRelayService(IpcBridge* bridge);
    ~RemoteCmdRelayService() override;
    
    bool initialize() override;
    bool start() override;
    void stop() override;
    ServiceState getState() const override;
    uint16_t getServiceId() const override;
    uint16_t getInstanceId() const override;
    
    // RemoteCmdRelay 特定方法
    void subscribeRemoteCommands(RemoteCommandCallback callback);
    void notifyRemoteCommand(const RemoteCommand& command);

private:
    void run();
    
    IpcBridge* bridge_;
    ServiceState state_;
    std::shared_ptr<ITspClient> tsp_client_;
    
    std::vector<RemoteCommandCallback> command_callbacks_;
    mutable std::mutex mutex_;
    
    std::atomic<bool> running_;
    std::thread worker_thread_;
};

} // namespace someip
} // namespace tbox
