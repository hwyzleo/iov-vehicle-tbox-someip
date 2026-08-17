// TspClientAdapter 实现（CR-005 / CR-008 / 配对 TSP CR-009）
//
// CR-008：旧 FOTA 专用 DTO/codec、reportSoftwareInventory/subscribeFotaCommand
// 适配已删除。通用 vehicle message 契约由真实 tbox::tsp_client 通用消息 API
// （exchangeVehicleMessage/subscribeVehicleMessage，配对 CR）提供；wire 只承载
// 单一序列化 Envelope bytes（base64 一次）。supportsVehicleMessage() 反映真实
// 能力，桥接不再静态 DEGRADED（US-013）；版本/daemon 就绪由运行时 outcome 表达。

#include "tsp_client_adapter.h"

#include <atomic>
#include <chrono>

namespace tbox {
namespace someip {

namespace {

// TSP TransportOutcome（tbox::tsp）→ SOMEIP TransportOutcome（tbox::someip）。
// 两套枚举值集一致，显式映射避免隐式跨命名空间依赖。
TransportOutcome map_tsp_outcome(tbox::tsp::TransportOutcome o) {
    switch (o) {
        case tbox::tsp::TransportOutcome::Accepted:        return TransportOutcome::Accepted;
        case tbox::tsp::TransportOutcome::Rejected:        return TransportOutcome::Rejected;
        case tbox::tsp::TransportOutcome::Timeout:         return TransportOutcome::Timeout;
        case tbox::tsp::TransportOutcome::Unknown:         return TransportOutcome::Unknown;
        case tbox::tsp::TransportOutcome::Unavailable:     return TransportOutcome::Unavailable;
        case tbox::tsp::TransportOutcome::VersionMismatch: return TransportOutcome::VersionMismatch;
        case tbox::tsp::TransportOutcome::PayloadTooLarge: return TransportOutcome::PayloadTooLarge;
        case tbox::tsp::TransportOutcome::ProtocolError:   return TransportOutcome::ProtocolError;
        case tbox::tsp::TransportOutcome::Stopping:        return TransportOutcome::Stopping;
        default:                                           return TransportOutcome::Unknown;
    }
}

} // namespace

TspClientAdapter::TspClientAdapter(const TspAdapterConfig& config)
    : config_(config)
    , tsp_client_(std::make_unique<tbox::tsp::TspClient>(config.socket_path)) {
}

TspClientAdapter::~TspClientAdapter() {
    shutdown();
}

// ============================================================
// IIpcClient
// ============================================================

bool TspClientAdapter::initialize() {
    if (!tsp_client_) {
        return false;
    }
    bool ok = tsp_client_->connect();
    SomeipLogAdapter::ipc_bridge().info("someip.ipc.tsp.connect",
        "TSP client adapter initialize",
        {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
         tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("initialize")),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString(ok ? "succeeded" : "failed"))}
    );
    return ok;
}

bool TspClientAdapter::isConnected() const {
    return tsp_client_ && tsp_client_->is_connected();
}

void TspClientAdapter::shutdown() {
    // 先取消下行订阅（RAII 句柄；独立连接随之断开）
    vehicle_subscription_.cancel();
    if (tsp_client_) {
        tsp_client_->disconnect();
    }
    SomeipLogAdapter::ipc_bridge().info("someip.ipc.tsp.shutdown",
        "TSP client adapter shutdown"
    );
}

// ============================================================
// CR-008 通用车云消息契约（真实 tbox::tsp_client 通用 API）
// ============================================================

bool TspClientAdapter::supportsVehicleMessage() const {
    // 配对 TSP CR 后真实通用 API 已随 SDK 提供：能力就绪。
    // 版本兼容/daemon 可达性由运行时 outcome（VersionMismatch/Unavailable）
    // 按请求体现，不再静态返回 false 进入永久 DEGRADED（US-013）。
    return tsp_client_ != nullptr;
}

VehicleMessageResponseFrame TspClientAdapter::exchangeVehicleMessage(
    const VehicleMessageFrame& frame) {

    VehicleMessageResponseFrame resp;
    if (!tsp_client_) {
        resp.outcome = TransportOutcome::Unavailable;
        resp.error = TransportError{static_cast<uint16_t>(ErrorCode::CLIENT_CALL_FAILED),
                                    "tsp_client_unavailable"};
        return resp;
    }

    tbox::tsp::VehicleMessage request;
    request.envelope_bytes = frame.envelope_bytes;

    tbox::tsp::ExchangeOptions options;
    options.timeout = std::chrono::milliseconds(config_.exchange_timeout_ms);
    options.max_response_bytes = config_.max_response_bytes;

    // 链路关联（非业务身份）：request_id 由本适配器生成用于中继关联；
    // 业务 messageId/correlationId/idempotencyKey/trace 保持在 Envelope 内由
    // TSP 原样透传（US-013：SOMEIP 不重新生成业务身份）。
    tbox::tsp::CallContext ctx;
    ctx.request_id = "someip-" + std::to_string(relay_seq_++);

    auto result = tsp_client_->exchangeVehicleMessage(request, options, ctx);

    resp.outcome = map_tsp_outcome(result.outcome);
    if (result.value.has_value()) {
        VehicleMessageFrame response;
        response.envelope_bytes = std::move(result.value->envelope_bytes);
        resp.response = std::move(response);
    }
    // 受控摘要（TSP 已脱敏，不记录完整 Envelope/payload/身份）
    if (result.error_code != 0 || !result.error.empty()) {
        std::string summary = result.error.empty()
            ? std::string("tsp_exchange_failed")
            : result.error;
        resp.error = TransportError{
            static_cast<uint16_t>(ErrorCode::CLIENT_CALL_FAILED),
            std::move(summary)};
    }
    return resp;
}

void TspClientAdapter::subscribeVehicleMessage(std::string_view service,
                                               VehicleMessageHandler handler) {
    if (!tsp_client_) {
        return;
    }

    // TSP 下行回调 → SOMEIP downlink handler（Envelope 透明转发）
    auto wrapped = [handler = std::move(handler)](const tbox::tsp::VehicleMessage& msg) {
        if (!handler) {
            return;
        }
        VehicleMessageFrame frame;
        frame.envelope_bytes = msg.envelope_bytes;
        handler(frame);
    };

    // 独立连接订阅（CR-009）；RAII 句柄成员持有，重订阅/析构自动取消旧订阅
    vehicle_subscription_ = tsp_client_->subscribeVehicleMessage(service, std::move(wrapped));

    SomeipLogAdapter::ipc_bridge().info("someip.ipc.tsp.subscribe",
        "TSP vehicle message downlink subscribed",
        {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
         tbox::fw::log::Field("service", tbox::fw::log::FieldValue::makeString(std::string(service))),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString(
             vehicle_subscription_.isActive() ? "active" : "inactive"))}
    );
}

// ============================================================
// 未迁移能力（占位，对应 facade 未就绪，属后续 CR）
// ============================================================

ErrorCode TspClientAdapter::getNetStatus(NetStatusData& status) {
    (void)status;
    return ErrorCode::SUCCESS;
}

void TspClientAdapter::subscribeNetStatusChanges(NetStatusCallback callback) {
    (void)callback;
}

void TspClientAdapter::subscribeRemoteCommands(RemoteCommandCallback callback) {
    (void)callback;
}

} // namespace someip
} // namespace tbox
