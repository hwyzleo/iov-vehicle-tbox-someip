#pragma once

#include "someip_types.h"
#include <memory>
#include <string>

namespace tbox {
namespace someip {

class SomeIpService {
public:
    virtual ~SomeIpService() = default;

    virtual bool initialize(const SomeIpServiceConfig& config) = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    virtual void registerMethodHandler(MethodId method_id, SomeIpRequestHandler handler) = 0;
    virtual void registerEventHandler(EventId event_id, SomeIpNotificationHandler handler) = 0;

    virtual bool offerService() = 0;
    virtual void stopOfferService() = 0;

    virtual ServiceId getServiceId() const = 0;
    virtual uint16_t getInstanceId() const = 0;

    /**
     * @brief 获取实际绑定的端口
     * @return 绑定的端口号，未初始化时返回 0
     */
    virtual uint16_t getBoundPort() const = 0;
};

// Factory function
std::unique_ptr<SomeIpService> createSomeIpService();

} // namespace someip
} // namespace tbox