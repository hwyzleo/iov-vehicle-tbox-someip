// VehicleMessageGateway 实现（CR-008）。

#include "vehicle_message_gateway.h"
#include "ipc_bridge.h"
#include "tbox_someip/someip_log_adapter.h"

#include <chrono>

namespace tbox {
namespace someip {

VehicleMessageGateway::VehicleMessageGateway(IpcBridge* bridge,
                                             uint16_t service_id,
                                             uint16_t instance_id,
                                             const GenericMessageConfig& cfg)
    : bridge_(bridge)
    , state_(ServiceState::UNAVAILABLE)
    , cfg_(cfg)
    , service_id_(service_id)
    , instance_id_(instance_id) {}

VehicleMessageGateway::~VehicleMessageGateway() {
    stop();
}

bool VehicleMessageGateway::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::UNAVAILABLE) {
        SomeipLogAdapter::vehicle_message().warn(
            "someip.vehicle_message.already_initialized",
            "VehicleMessageGateway already initialized");
        return false;
    }

    if (!bridge_ || !bridge_->isInitialized()) {
        SomeipLogAdapter::ipc_bridge().error(
            "someip.ipc.call.failed",
            "IpcBridge not initialized",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("initialize")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))});
        return false;
    }

    auto client = bridge_->getClient("tsp");
    if (!client) {
        SomeipLogAdapter::ipc_bridge().error(
            "someip.ipc.call.failed",
            "TSP client not found",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("get_client")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))});
        return false;
    }

    tsp_client_ = std::dynamic_pointer_cast<ITspClient>(client);
    if (!tsp_client_) {
        SomeipLogAdapter::ipc_bridge().error(
            "someip.ipc.call.failed",
            "Invalid TSP client type",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("cast")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))});
        return false;
    }

    if (!tsp_client_->initialize()) {
        // 非致命：后端 daemon 未就绪时按需重连，不阻塞门面启动
        SomeipLogAdapter::ipc_bridge().warn(
            "someip.ipc.tsp.connect_deferred",
            "TSP client not connected yet; will reconnect on demand",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("initialize")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("deferred"))});
    }

    if (!bridge_impl_.initialize(tsp_client_.get(), cfg_)) {
        SomeipLogAdapter::vehicle_message().error(
            "someip.vehicle_message.init_failed",
            "GenericMessageBridge init failed",
            {tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1004"))});
        return false;
    }

    state_ = ServiceState::INITIALIZING;
    SomeipLogAdapter::vehicle_message().info(
        "someip.vehicle_message.initialized",
        "VehicleMessageGateway initialized");
    return true;
}

bool VehicleMessageGateway::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::INITIALIZING) {
        SomeipLogAdapter::vehicle_message().warn(
            "someip.vehicle_message.start_failed",
            "VehicleMessageGateway not in initializing state");
        return false;
    }

    bridge_impl_.start();  // TSP 能力探测 + 下行订阅 + worker

    state_ = ServiceState::AVAILABLE;
    SomeipLogAdapter::vehicle_message().info(
        "someip.vehicle_message.started",
        "VehicleMessageGateway started",
        {tbox::fw::log::Field("degraded", tbox::fw::log::FieldValue::makeBool(bridge_impl_.isDegraded()))});
    return true;
}

void VehicleMessageGateway::stop() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ == ServiceState::UNAVAILABLE) {
        return;
    }

    // 顺序（CR-008 §生命周期/停止）：先停 bridge（worker/队列/handler），
    // TSP client 生命周期由 IpcBridge::shutdown 统一收敛（取消 RAII 订阅）。
    bridge_impl_.stop();
    tsp_client_.reset();

    state_ = ServiceState::UNAVAILABLE;
    SomeipLogAdapter::vehicle_message().info(
        "someip.vehicle_message.stopped",
        "VehicleMessageGateway stopped");
}

ServiceState VehicleMessageGateway::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

uint16_t VehicleMessageGateway::getServiceId() const {
    return service_id_;
}

uint16_t VehicleMessageGateway::getInstanceId() const {
    return instance_id_;
}

VehicleMessageResponseFrame VehicleMessageGateway::exchangeVehicleMessage(
    const VehicleMessageFrame& frame) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ != ServiceState::AVAILABLE) {
            VehicleMessageResponseFrame resp;
            resp.outcome = TransportOutcome::Unavailable;
            resp.error = TransportError{static_cast<uint16_t>(ErrorCode::CLIENT_CALL_FAILED),
                                        "gateway_not_available"};
            return resp;
        }
    }
    // 锁外调用 bridge（内部以 in-flight 原子预算自洽，避免阻塞其他能力）
    return bridge_impl_.exchange(frame);
}

void VehicleMessageGateway::setDownlinkHandler(VehicleMessageHandler handler) {
    bridge_impl_.setDownlinkHandler(std::move(handler));
}

bool VehicleMessageGateway::isDegraded() const {
    return bridge_impl_.isDegraded();
}

} // namespace someip
} // namespace tbox
