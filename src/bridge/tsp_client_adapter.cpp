// TspClientAdapter 实现（CR-005 / CR-008）
//
// CR-008：旧 FOTA 专用 DTO/codec、reportSoftwareInventory/subscribeFotaCommand
// 适配已删除。通用 vehicle message 契约由后续版本化 tbox::tsp_client 通用消息
// API（配对 CR）提供；当前真实 facade 无该能力，supportsVehicleMessage()=false，
// 桥接 DEGRADED（US-013）。测试用 Fake 通用 client 验证完整桥接逻辑。

#include "tsp_client_adapter.h"

namespace tbox {
namespace someip {

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
    if (tsp_client_) {
        tsp_client_->disconnect();
    }
    SomeipLogAdapter::ipc_bridge().info("someip.ipc.tsp.shutdown",
        "TSP client adapter shutdown"
    );
}

// ============================================================
// CR-008 通用车云消息契约
// ============================================================

bool TspClientAdapter::supportsVehicleMessage() const {
    // 后续版本化 tbox::tsp_client 通用消息 API（配对 CR）尚未提供：
    // 能力未就绪 → false → GenericMessageBridge DEGRADED（US-013），
    // 对外返回 Unavailable/VersionMismatch，不回退解析 OTA payload。
    return false;
}

VehicleMessageResponseFrame TspClientAdapter::exchangeVehicleMessage(
    const VehicleMessageFrame& frame) {
    (void)frame;
    VehicleMessageResponseFrame resp;
    resp.outcome = TransportOutcome::Unavailable;
    resp.error = TransportError{static_cast<uint16_t>(ErrorCode::CLIENT_CALL_FAILED),
                                "tsp_generic_capability_unavailable"};
    return resp;
}

void TspClientAdapter::subscribeVehicleMessage(std::string_view service,
                                               VehicleMessageHandler handler) {
    (void)service;
    (void)handler;
    // 能力未就绪：不建立下行订阅（DEGRADED 语义）
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
