// GenericMessageBridge 实现（CR-008）。
// 日志只记录 service、payloadType/route 摘要、message/correlation 摘要、长度、
// outcome、耗时、queue depth 与错误码；不记录完整 Envelope、payload、VIN/UID、
// URL/token、凭据或控制内容（CR-008 §日志与指标）。

#include "generic_message_bridge.h"
#include "tbox_someip/someip_log_adapter.h"

#include <chrono>
#include <string>

namespace tbox {
namespace someip {

GenericMessageBridge::GenericMessageBridge() = default;
GenericMessageBridge::~GenericMessageBridge() {
    stop();
}

bool GenericMessageBridge::initialize(ITspClient* tsp, const GenericMessageConfig& cfg) {
    if (initialized_) {
        return false;
    }
    tsp_ = tsp;
    config_ = cfg;
    validator_ = VehicleMessageValidator(cfg.limits);

    // 严格校验 tsp_timeout_ms < request_deadline_ms（CR-008 §配置增量）
    if (cfg.tsp_timeout_ms >= cfg.request_deadline_ms) {
        SomeipLogAdapter::vehicle_message().error(
            "someip.vehicle_message.config_invalid",
            "tsp_timeout_ms must be < request_deadline_ms",
            {tbox::fw::log::Field("tsp_timeout_ms", tbox::fw::log::FieldValue::makeInt(cfg.tsp_timeout_ms)),
             tbox::fw::log::Field("request_deadline_ms", tbox::fw::log::FieldValue::makeInt(cfg.request_deadline_ms))}
        );
        return false;
    }

    initialized_ = true;
    return true;
}

void GenericMessageBridge::start() {
    if (!initialized_ || running_) {
        return;
    }
    running_ = true;
    stopping_ = false;

    // TSP 通用能力探测（US-013）：能力未就绪/版本不兼容 → DEGRADED
    if (tsp_ && tsp_->supportsVehicleMessage()) {
        degraded_ = false;
        // 下行独立订阅（RAII 在 adapter 内；capability 确认后才订阅）
        tsp_->subscribeVehicleMessage(
            "vehicle.fota",
            [this](const VehicleMessageFrame& frame) { onTspDownlink(frame); });

        // 有界等待订阅握手收敛（本地 socket 毫秒级；最多 ~200ms）：订阅被服务端
        // 拒绝（isActive=false）→ DEGRADED（US-013 不得宣称契约 B 可用）。
        // daemon 不可达时订阅连接重试中（isActive 保持 true），运行期以
        // Unavailable outcome 表达，不静态 DEGRADED。
        for (int i = 0; i < kSubscribeSettleTicks; ++i) {
            if (!tsp_->isVehicleMessageDownlinkActive()) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kSubscribeSettleIntervalMs));
        }
        degraded_ = !tsp_->isVehicleMessageDownlinkActive();

        SomeipLogAdapter::vehicle_message().info(
            "someip.vehicle_message.tsp.ready",
            "TSP generic vehicle message capability ready",
            {tbox::fw::log::Field("service", tbox::fw::log::FieldValue::makeString("vehicle.fota")),
             tbox::fw::log::Field("downlink_active", tbox::fw::log::FieldValue::makeBool(
                 !degraded_.load())),
             tbox::fw::log::Field("allowed_services",
                 tbox::fw::log::FieldValue::makeString(config_.limits.allowed_services.empty()
                     ? std::string() : config_.limits.allowed_services.front()))}
        );
    } else {
        degraded_ = true;
        SomeipLogAdapter::vehicle_message().warn(
            "someip.vehicle_message.tsp.degraded",
            "TSP generic vehicle message capability unavailable; bridge DEGRADED",
            {tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("degraded")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
    }

    // 启动下行 worker（无论 DEGRADED 与否，保持单一确定性生命周期）
    worker_thread_ = std::thread(&GenericMessageBridge::downlinkWorkerLoop, this);
}

void GenericMessageBridge::stop() {
    if (!running_.exchange(false)) {
        return;
    }
    stopping_ = true;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_cv_.notify_all();
    }
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        downlink_queue_ = std::queue<VehicleMessageFrame>();
    }
    {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        downlink_handler_ = nullptr;
    }
    in_flight_ = 0;
}

bool GenericMessageBridge::isDegraded() const {
    return degraded_.load();
}

bool GenericMessageBridge::isReady() const {
    return running_.load() && !degraded_.load();
}

VehicleMessageResponseFrame GenericMessageBridge::exchange(
    const VehicleMessageFrame& frame) {

    VehicleMessageResponseFrame resp;

    // 停止中拒绝新请求（Stopping）
    if (stopping_.load() || !running_.load()) {
        resp.outcome = TransportOutcome::Stopping;
        resp.error = TransportError{static_cast<uint16_t>(ErrorCode::CLIENT_CALL_FAILED), "stopping"};
        return resp;
    }

    // DEGRADED：TSP 通用能力未就绪，明确失败，不回退旧路径
    if (degraded_.load() || !tsp_ || !tsp_->supportsVehicleMessage()) {
        resp.outcome = TransportOutcome::Unavailable;
        resp.error = TransportError{static_cast<uint16_t>(ErrorCode::CLIENT_CALL_FAILED),
                                    "tsp_generic_capability_unavailable"};
        SomeipLogAdapter::vehicle_message().warn(
            "someip.vehicle_message.tsp.degraded",
            "Vehicle message exchange rejected: TSP generic capability unavailable",
            {tbox::fw::log::Field("outcome", tbox::fw::log::FieldValue::makeString("unavailable")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return resp;
    }

    // 校验管线（IDL/长度/解析/major/kind/service/payloadType/TTL/correlation/payload）
    VehicleMessageValidation v = validator_.validate(frame);
    if (!v.ok) {
        resp.outcome = v.outcome;
        resp.error = TransportError{VehicleMessageValidator::outcomeErrorCode(v.outcome), v.reason};
        const char* ev = "someip.vehicle_message.request.rejected";
        switch (v.outcome) {
            case TransportOutcome::Rejected:
                if (v.reason == "ttl_expired") ev = "someip.vehicle_message.ttl_expired";
                break;
            case TransportOutcome::PayloadTooLarge:
                ev = "someip.vehicle_message.payload_too_large";
                break;
            case TransportOutcome::VersionMismatch:
                ev = "someip.vehicle_message.version_mismatch";
                break;
            default:
                break;
        }
        SomeipLogAdapter::vehicle_message().warn(
            ev,
            "Vehicle message request rejected",
            {tbox::fw::log::Field("reason", tbox::fw::log::FieldValue::makeString(v.reason)),
             tbox::fw::log::Field("outcome", tbox::fw::log::FieldValue::makeString("rejected")),
             tbox::fw::log::Field("payload_length", tbox::fw::log::FieldValue::makeInt(
                 static_cast<int64_t>(v.envelope.payload.size()))),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString(
                 "TBOX-SOMEIP-" + std::to_string(VehicleMessageValidator::outcomeErrorCode(v.outcome))))}
        );
        return resp;
    }

    // in-flight 预算（有界并发；队列/in-flight 满 → Rejected）
    uint32_t cur = in_flight_.load();
    while (cur < config_.max_in_flight) {
        if (in_flight_.compare_exchange_weak(cur, cur + 1)) {
            break;
        }
    }
    if (cur >= config_.max_in_flight) {
        resp.outcome = TransportOutcome::Rejected;
        resp.error = TransportError{static_cast<uint16_t>(ErrorCode::CLIENT_CALL_FAILED), "in_flight_full"};
        SomeipLogAdapter::vehicle_message().warn(
            "someip.vehicle_message.queue_full",
            "Vehicle message exchange rejected: in-flight budget full",
            {tbox::fw::log::Field("max_in_flight", tbox::fw::log::FieldValue::makeInt(config_.max_in_flight)),
             tbox::fw::log::Field("outcome", tbox::fw::log::FieldValue::makeString("rejected"))}
        );
        return resp;
    }
    struct InFlightGuard {
        std::atomic<uint32_t>& n;
        ~InFlightGuard() { n.fetch_sub(1); }
    } guard{in_flight_};

    const auto start_time = std::chrono::steady_clock::now();
    SomeipLogAdapter::vehicle_message().info(
        "someip.vehicle_message.request.received",
        "Vehicle message request received",
        {tbox::fw::log::Field("service", tbox::fw::log::FieldValue::makeString(v.envelope.service)),
         tbox::fw::log::Field("payload_type", tbox::fw::log::FieldValue::makeString(v.envelope.payload_type)),
         tbox::fw::log::Field("message_id", tbox::fw::log::FieldValue::makeString(v.envelope.message_id)),
         tbox::fw::log::Field("envelope_length", tbox::fw::log::FieldValue::makeInt(
             static_cast<int64_t>(v.envelope.serialized_length)))}
    );

    // 调用 TSP 通用 exchange（保持 messageId/correlationId/idempotencyKey/trace）
    VehicleMessageResponseFrame tsp_resp = tsp_->exchangeVehicleMessage(frame);
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();

    resp = tsp_resp;
    if (resp.outcome != TransportOutcome::Accepted) {
        // 明确 unknown/timeout/rejected/unavailable 等，不生成新业务请求重试
        const char* ev = "someip.vehicle_message.tsp.rejected";
        if (resp.outcome == TransportOutcome::Timeout ||
            resp.outcome == TransportOutcome::Unknown) {
            ev = "someip.vehicle_message.tsp.unknown";
        }
        SomeipLogAdapter::vehicle_message().warn(
            ev,
            "Vehicle message TSP bridge not accepted",
            {tbox::fw::log::Field("outcome", tbox::fw::log::FieldValue::makeString(
                 resp.outcome == TransportOutcome::Accepted ? "accepted" :
                 resp.outcome == TransportOutcome::Timeout ? "timeout" :
                 resp.outcome == TransportOutcome::Unknown ? "unknown" : "rejected")),
             tbox::fw::log::Field("message_id", tbox::fw::log::FieldValue::makeString(v.envelope.message_id)),
             tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(duration_ms)),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString(
                 resp.error ? "TBOX-SOMEIP-" + std::to_string(resp.error->code) : ""))}
        );
        return resp;
    }

    // correlation 校验：response Envelope 的 correlation_id 必须指向请求 message_id
    // （US-014：迟到/重复/correlation 错配不得完成其他请求）
    if (resp.response.has_value() && !resp.response->envelope_bytes.empty()) {
        VehicleMessageValidation rv = validator_.validate(resp.response.value());
        if (rv.ok) {
            if (rv.envelope.correlation_id != v.envelope.message_id) {
                resp.outcome = TransportOutcome::ProtocolError;
                resp.error = TransportError{static_cast<uint16_t>(ErrorCode::SERIALIZATION_FAILED),
                                            "correlation_mismatch"};
                SomeipLogAdapter::vehicle_message().warn(
                    "someip.vehicle_message.correlation_mismatch",
                    "Response correlation_id does not match request message_id",
                    {tbox::fw::log::Field("request_message_id",
                         tbox::fw::log::FieldValue::makeString(v.envelope.message_id)),
                     tbox::fw::log::Field("response_correlation_id",
                         tbox::fw::log::FieldValue::makeString(rv.envelope.correlation_id))}
                );
                return resp;
            }
        } else {
            resp.outcome = TransportOutcome::ProtocolError;
            resp.error = TransportError{static_cast<uint16_t>(ErrorCode::SERIALIZATION_FAILED),
                                        rv.reason.empty() ? "response_envelope_invalid" : rv.reason};
            return resp;
        }
    }

    SomeipLogAdapter::vehicle_message().info(
        "someip.vehicle_message.request.completed",
        "Vehicle message request completed",
        {tbox::fw::log::Field("service", tbox::fw::log::FieldValue::makeString(v.envelope.service)),
         tbox::fw::log::Field("payload_type", tbox::fw::log::FieldValue::makeString(v.envelope.payload_type)),
         tbox::fw::log::Field("message_id", tbox::fw::log::FieldValue::makeString(v.envelope.message_id)),
         tbox::fw::log::Field("outcome", tbox::fw::log::FieldValue::makeString("accepted")),
         tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(duration_ms))}
    );
    return resp;
}

void GenericMessageBridge::setDownlinkHandler(VehicleMessageHandler handler) {
    std::lock_guard<std::mutex> lock(handler_mutex_);
    downlink_handler_ = std::move(handler);
}

// TSP 下行 subscription callback：只做最小校验并入有界队列，绝不阻塞
void GenericMessageBridge::onTspDownlink(const VehicleMessageFrame& frame) {
    if (!running_.load() || stopping_.load()) {
        return;  // Stop 状态不阻塞 TSP callback
    }

    // 最小 Envelope/大小/TTL 校验（下行同用完整校验管线，payload 不解析）
    VehicleMessageValidation v = validator_.validate(frame);
    if (!v.ok) {
        const char* ev = "someip.vehicle_message.downlink.dropped";
        if (v.outcome == TransportOutcome::Rejected && v.reason == "ttl_expired") {
            ev = "someip.vehicle_message.ttl_expired";
        } else if (v.outcome == TransportOutcome::PayloadTooLarge) {
            ev = "someip.vehicle_message.payload_too_large";
        }
        SomeipLogAdapter::vehicle_message().warn(
            ev,
            "Downlink dropped: validation failed",
            {tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("down")),
             tbox::fw::log::Field("reason", tbox::fw::log::FieldValue::makeString(v.reason))}
        );
        return;
    }

    SomeipLogAdapter::vehicle_message().info(
        "someip.vehicle_message.downlink.received",
        "Downlink vehicle message received from TSP",
        {tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("down")),
         tbox::fw::log::Field("service", tbox::fw::log::FieldValue::makeString(v.envelope.service)),
         tbox::fw::log::Field("payload_type", tbox::fw::log::FieldValue::makeString(v.envelope.payload_type)),
         tbox::fw::log::Field("message_id", tbox::fw::log::FieldValue::makeString(v.envelope.message_id))}
    );

    // 有界队列（满则丢旧保序，慢 CGW 不阻塞 TSP callback）
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        if (downlink_queue_.size() >= config_.downlink_queue_capacity) {
            downlink_queue_.pop();
            SomeipLogAdapter::vehicle_message().warn(
                "someip.vehicle_message.queue_full",
                "Downlink queue full; dropping oldest to keep order",
                {tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("down")),
                 tbox::fw::log::Field("queue_capacity", tbox::fw::log::FieldValue::makeInt(
                     static_cast<int64_t>(config_.downlink_queue_capacity)))}
            );
        }
        downlink_queue_.push(frame);
    }
    queue_cv_.notify_one();
}

void GenericMessageBridge::downlinkWorkerLoop() {
    while (running_.load()) {
        VehicleMessageFrame frame;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !downlink_queue_.empty() || !running_.load();
            });
            if (!running_.load() && downlink_queue_.empty()) {
                break;
            }
            if (downlink_queue_.empty()) {
                continue;
            }
            frame = std::move(downlink_queue_.front());
            downlink_queue_.pop();
        }

        VehicleMessageHandler handler;
        {
            std::lock_guard<std::mutex> lock(handler_mutex_);
            handler = downlink_handler_;
        }
        if (handler) {
            handler(frame);
            SomeipLogAdapter::vehicle_message().info(
                "someip.vehicle_message.downlink.forwarded",
                "Downlink vehicle message forwarded to CGW",
                {tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("down")),
                 tbox::fw::log::Field("envelope_length", tbox::fw::log::FieldValue::makeInt(
                     static_cast<int64_t>(frame.envelope_bytes.size())))}
            );
        } else {
            SomeipLogAdapter::vehicle_message().warn(
                "someip.vehicle_message.downlink.dropped",
                "Downlink dropped: no handler",
                {tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("down"))}
            );
        }
    }
}

} // namespace someip
} // namespace tbox
