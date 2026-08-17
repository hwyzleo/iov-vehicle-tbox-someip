#pragma once

// TspClientAdapter（CR-005 / CR-008 / 配对 TSP CR-009）
//
// 通过 tbox::tsp::TspClient（framework-ipc 域 facade）访问 TSP daemon。
//
// 边界：
// - 不直接创建 framework Client、不解析 provider wire、不解析业务 Topic。
// - CR-008 + TSP 配对 CR：通用 vehicle message exchange/downlink 走真实
//   tbox::tsp_client 通用 API（exchangeVehicleMessage/subscribeVehicleMessage）。
//   supportsVehicleMessage() 反映真实能力（SDK 提供即 true）；版本兼容与 daemon
//   就绪由运行时 outcome（VersionMismatch/Unavailable）按请求体现，不再静态
//   DEGRADED（US-013）。IPC wire 只承载单一 Envelope bytes（base64 一次）。
// - 未迁移能力（NetStatus/RemoteCmd）保持占位行为。

#include "tbox_someip/interfaces.h"
#include "tbox_someip/types.h"
#include "tbox_someip/someip_log_adapter.h"

#include "tbox/tsp/client.h"   // TspClient
#include "ipc.h"               // tbox::fw::ipc::Subscription

#include <atomic>
#include <memory>
#include <string>

namespace tbox {
namespace someip {

/// TSP 适配器配置
struct TspAdapterConfig {
    std::string socket_path = "/tmp/tbox-tsp.sock";
    uint32_t default_timeout_ms = 3000;
    uint32_t event_queue_size = 256;

    // CR-008：ExchangeOptions 上送 TSP（须 < SOME/IP Method deadline）
    uint32_t exchange_timeout_ms = 2500;       // 对齐 generic_message.tsp_timeout_ms
    uint32_t max_response_bytes = 16384;       // 对齐 generic_message.max_envelope_bytes
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
    bool isVehicleMessageDownlinkActive() const override;

    // ---- 未迁移能力（占位，对应 facade 未就绪）----
    ErrorCode getNetStatus(NetStatusData& status) override;
    void subscribeNetStatusChanges(NetStatusCallback callback) override;
    void subscribeRemoteCommands(RemoteCommandCallback callback) override;

private:
    TspAdapterConfig config_;
    std::unique_ptr<tbox::tsp::TspClient> tsp_client_;

    // 下行订阅 RAII 句柄（析构/重订阅自动取消旧订阅；CR-009 独立连接）
    tbox::fw::ipc::Subscription vehicle_subscription_;

    // 中继 request_id 生成（非业务身份，仅链路关联；业务 messageId/幂等键在 Envelope 内）
    std::atomic<uint64_t> relay_seq_{0};
};

} // namespace someip
} // namespace tbox
