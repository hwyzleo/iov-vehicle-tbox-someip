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

    // 事件组注册（CR-008 EVENT_VEHICLE_MESSAGE 下行）。精确 Eventgroup ID 由
    // Registry 分配，注册须在 OfferService 前完成；未注册 Eventgroup 的 Event
    // 不得对外 Offer/notify（fail-closed，不猜测默认 Eventgroup 值）。
    virtual void registerEventGroup(EventId event_id, uint32_t eventgroup_id) = 0;

    // 注册/Offer 状态只读查询（诊断与测试）：
    //   hasMethodHandler / hasEventHandler —— 该 member 是否已注册 handler
    //   hasEventGroup —— 该 Event 是否已注册 Eventgroup 并允许 notify/Offer
    virtual bool hasMethodHandler(MethodId method_id) const = 0;
    virtual bool hasEventHandler(EventId event_id) const = 0;
    virtual bool hasEventGroup(EventId event_id) const = 0;

    // 发布事件通知（CR-008 EVENT_VEHICLE_MESSAGE 下行通道）
    virtual bool notifyEvent(EventId event_id, const SomeIpMessage& notification) = 0;

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