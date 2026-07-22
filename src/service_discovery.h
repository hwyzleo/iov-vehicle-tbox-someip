#pragma once

#include "tbox_someip/interfaces.h"
#include <memory>
#include <map>
#include <mutex>
#include <functional>

namespace tbox {
namespace someip {

class ServiceDiscovery : public IServiceDiscovery {
public:
    ServiceDiscovery();
    ~ServiceDiscovery() override;
    
    bool initialize() override;
    bool offerService(uint16_t service_id, uint16_t instance_id) override;
    void stopOfferService(uint16_t service_id, uint16_t instance_id) override;
    void subscribeServiceAvailability(
        uint16_t service_id,
        uint16_t instance_id,
        std::function<void(bool)> callback
    ) override;
    void shutdown() override;

private:
    bool initialized_;
    std::map<std::pair<uint16_t, uint16_t>, std::function<void(bool)>> availability_callbacks_;
    mutable std::mutex mutex_;
};

} // namespace someip
} // namespace tbox
