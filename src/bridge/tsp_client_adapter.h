#pragma once

// TspClientAdapter（CR-005 / CR-008）
//
// 通过 tbox::tsp::TspClient（framework-ipc 域 facade）访问 TSP daemon。
//
// 边界：
// - 不直接创建 framework Client、不解析 provider wire、不解析业务 Topic。
// - CR-008：旧 FOTA 专用 reportSoftwareInventory/subscribeFotaCommand 适配已
//   删除。通用 vehicle message exchange/downlink 依赖后续版本化 tbox::tsp_client
//   通用消息 API（配对 CR）；当前真实 facade 尚无该能力，supportsVehicleMessage()
//   返回 false → GenericMessageBridge 进入 DEGRADED（对外 VersionMismatch/
//   Unavailable，不回退旧路径，US-013）。单元/集成测试使用 Fake 通用 client 验证。
// - 未迁移能力（NetStatus/RemoteCmd）保持占位行为。

#include "tbox_someip/interfaces.h"
#include "tbox_someip/types.h"
#include "tbox_someip/someip_log_adapter.h"

#include "tbox/tsp/client.h"   // TspClient
#include "ipc.h"               // tbox::fw::ipc::Subscription

#include <memory>
#include <string>

namespace tbox {
namespace someip {

/// TSP 适配器配置
struct TspAdapterConfig {
    std::string socket_path = "/tmp/tbox-tsp.sock";
    uint32_t default_timeout_ms = 3000;
    uint32_t event_queue_size = 256;
};

class TspClientAdapter : public ITspClient {
public:
    explicit TspClientAdapter(const TspAdapterConfig& config = TspAdapterConfig{});
    ~TspClientAdapter() override;

    TspClientAdapter(const TspClientAdapter&) = delete;
    TspClientAdapter& operator=(const TspClientAdapter&) = delete;

    // ---- IIpcClient ----
    bool initialize() override;
    bool isConnected() const override;
    void shutdown() override;

    // ---- CR-008 通用车云消息契约 ----
    bool supportsVehicleMessage() const override;
    VehicleMessageResponseFrame exchangeVehicleMessage(
        const VehicleMessageFrame& frame) override;
    void subscribeVehicleMessage(std::string_view service,
                                 VehicleMessageHandler handler) override;

    // ---- 未迁移能力（占位，对应 facade 未就绪）----
    ErrorCode getNetStatus(NetStatusData& status) override;
    void subscribeNetStatusChanges(NetStatusCallback callback) override;
    void subscribeRemoteCommands(RemoteCommandCallback callback) override;

private:
    TspAdapterConfig config_;
    std::unique_ptr<tbox::tsp::TspClient> tsp_client_;
};

} // namespace someip
} // namespace tbox
