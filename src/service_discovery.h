#pragma once

#include "tbox_someip/interfaces.h"
#include "tbox_someip/someip_service.h"
#include <memory>
#include <map>
#include <mutex>
#include <functional>

namespace tbox {
namespace someip {

class ServiceDiscovery : public IServiceDiscovery {
public:
    explicit ServiceDiscovery(uint16_t port);
    ~ServiceDiscovery() override;
    
    bool initialize() override;
    bool offerService(uint16_t service_id, uint16_t instance_id) override;
    void stopOfferService(uint16_t service_id, uint16_t instance_id) override;
    void subscribeServiceAvailability(
        uint16_t service_id,
        uint16_t instance_id,
        std::function<void(bool)> callback
    ) override;
    void subscribeClientConnection(
        uint16_t service_id,
        uint16_t instance_id,
        ClientConnectionCallback callback
    ) override;
    void simulateClientConnection(uint16_t service_id, uint16_t instance_id, uint16_t client_id, bool connected) override;
    void shutdown() override;

    /**
     * @brief 注册 SOME/IP 方法处理器
     * @param method_id 方法 ID
     * @param handler 处理函数
     */
    void registerMethodHandler(MethodId method_id, SomeIpRequestHandler handler);

    /**
     * @brief 注册 SOME/IP 事件处理器
     */
    void registerEventHandler(EventId event_id, SomeIpNotificationHandler handler);

    /**
     * @brief 注册事件组（CR-008 EVENT_VEHICLE_MESSAGE 下行；精确 Eventgroup ID 由
     * Registry 分配，注册在 OfferService 前完成；未注册 Eventgroup 的 Event 不 Offer）
     */
    void registerEventGroup(EventId event_id, uint32_t eventgroup_id);

    /**
     * @brief 注册/Offer 状态只读查询（诊断与测试）
     */
    bool hasMethodHandler(MethodId method_id) const;
    bool hasEventHandler(EventId event_id) const;
    bool hasEventGroup(EventId event_id) const;

    /**
     * @brief 发布事件通知（CR-008 EVENT_VEHICLE_MESSAGE 下行通道）
     */
    bool notifyEvent(EventId event_id, const SomeIpMessage& notification);

private:
    bool initialized_;
    std::map<std::pair<uint16_t, uint16_t>, std::function<void(bool)>> availability_callbacks_;
    std::map<std::pair<uint16_t, uint16_t>, ClientConnectionCallback> client_connection_callbacks_;
    mutable std::mutex mutex_;
    
    // SOME/IP 服务
    std::unique_ptr<SomeIpService> someip_service_;

    // 业务端口（CR-007 方案 C：从 config 读取，替代硬编码 56101）
    uint16_t port_;
};

} // namespace someip
} // namespace tbox
