#pragma once

// VehicleMessageGateway（CR-008）：聚合 Service 0x6101 内通用车云消息门面。
// 取代旧 FotaRelayService（reportSoftwareInventory/onFotaCommand 已删除，旧
// 0x0001/0x8001 reserved）。对外暴露 exchangeVehicleMessage Method 与
// onVehicleMessage Event，内部经 GenericMessageBridge + tbox::tsp_client 通用
// 契约桥接；TSP 通用能力未就绪时 DEGRADED（Unavailable/VersionMismatch）。

#include "tbox_someip/interfaces.h"
#include "tbox_someip/types.h"
#include "bridge/generic_message_bridge.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace tbox {
namespace someip {

class IpcBridge;

class VehicleMessageGateway : public ISomeipService {
public:
    VehicleMessageGateway(IpcBridge* bridge,
                          uint16_t service_id,
                          uint16_t instance_id,
                          const GenericMessageConfig& cfg);
    ~VehicleMessageGateway() override;

    bool initialize() override;
    bool start() override;
    void stop() override;
    ServiceState getState() const override;
    uint16_t getServiceId() const override;
    uint16_t getInstanceId() const override;

    // 上行：通用 Method 入口（daemon method handler 调用）
    VehicleMessageResponseFrame exchangeVehicleMessage(const VehicleMessageFrame& frame);

    // 下行：daemon 把 handler 接到 notifyEvent（EVENT_VEHICLE_MESSAGE）
    void setDownlinkHandler(VehicleMessageHandler handler);

    // TSP 通用能力是否 DEGRADED（US-013）
    bool isDegraded() const;

private:
    IpcBridge* bridge_;
    ServiceState state_;
    std::shared_ptr<ITspClient> tsp_client_;
    GenericMessageConfig cfg_;
    GenericMessageBridge bridge_impl_;
    mutable std::mutex mutex_;

    uint16_t service_id_;
    uint16_t instance_id_;
};

} // namespace someip
} // namespace tbox
