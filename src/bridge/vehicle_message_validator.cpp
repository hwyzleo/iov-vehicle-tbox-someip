// 公共 Envelope 解码与准入校验实现（CR-008）。

#include "vehicle_message_validator.h"

#include <chrono>
#include <regex>

#include "vehicle/common/v1/vehicle_message_envelope.pb.h"

namespace tbox {
namespace someip {

namespace {

// vehicle.fota.v1.<MessageName> fully-qualified name（US-012）：
// 以 service+'.v1.' 前缀 + 单个合法标识符，禁止任意嵌套/非 FQN。
const std::regex kPayloadTypeName("^[A-Za-z_][A-Za-z0-9_]*$");

bool containsService(const std::vector<std::string>& services,
                     const std::string& svc) {
    for (const auto& s : services) {
        if (s == svc) return true;
    }
    return false;
}

bool containsMajor(const std::vector<uint32_t>& majors, uint32_t major) {
    for (auto m : majors) {
        if (m == major) return true;
    }
    return false;
}

std::vector<std::byte> toBytes(const std::string& s) {
    std::vector<std::byte> out(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        out[i] = static_cast<std::byte>(static_cast<uint8_t>(s[i]));
    }
    return out;
}

} // namespace

VehicleMessageValidator::VehicleMessageValidator(const VehicleMessageLimits& limits)
    : limits_(limits) {}

uint16_t VehicleMessageValidator::outcomeErrorCode(TransportOutcome o) {
    switch (o) {
        case TransportOutcome::Accepted:
            return 0;
        case TransportOutcome::Timeout:
            return static_cast<uint16_t>(ErrorCode::CLIENT_CALL_TIMEOUT);   // 1002
        case TransportOutcome::Unavailable:
        case TransportOutcome::VersionMismatch:
            return static_cast<uint16_t>(ErrorCode::CLIENT_CALL_FAILED);    // 1003
        case TransportOutcome::ProtocolError:
            return static_cast<uint16_t>(ErrorCode::SERIALIZATION_FAILED);  // 1004
        default:
            return static_cast<uint16_t>(ErrorCode::CLIENT_CALL_FAILED);    // 1003
    }
}

VehicleMessageValidation VehicleMessageValidator::validate(
    const VehicleMessageFrame& frame) const {

    VehicleMessageValidation result;
    const size_t total_len = frame.envelope_bytes.size();

    // 1) IDL/总长度校验（有界；超限明确失败，不截断、不分片、不落盘）
    if (total_len > limits_.max_envelope_bytes) {
        result.ok = false;
        result.outcome = TransportOutcome::PayloadTooLarge;
        result.reason = "envelope_exceeds_max_bytes";
        return result;
    }

    // 2) proto 解析 / 初始化校验
    vehicle::common::v1::VehicleMessageEnvelope env;
    if (total_len == 0) {
        result.ok = false;
        result.outcome = TransportOutcome::ProtocolError;
        result.reason = "empty_envelope";
        return result;
    }
    {
        std::string raw(reinterpret_cast<const char*>(frame.envelope_bytes.data()),
                        total_len);
        if (!env.ParseFromString(raw)) {
            result.ok = false;
            result.outcome = TransportOutcome::ProtocolError;
            result.reason = "envelope_parse_failed";
            return result;
        }
    }

    auto& e = result.envelope;
    e.request_id = env.request_id();
    e.timestamp_ms = env.timestamp_ms();
    e.protocol_version = env.protocol_version();
    e.device_id = env.device_id();
    e.vin = env.vin();
    e.vehicle_task_id = env.vehicle_task_id();
    e.execution_id = env.execution_id();
    e.idempotency_key = env.idempotency_key();
    e.payload_type = env.payload_type();
    e.payload = toBytes(env.payload());
    e.message_id = env.message_id();
    e.correlation_id = env.correlation_id();
    e.message_kind = env.message_kind();
    e.service = env.service();
    e.expire_at_ms = env.expire_at_ms();
    e.trace_context = env.trace_context();
    e.serialized_length = total_len;

    // 3) Envelope major / 初始化校验
    if (!containsMajor(limits_.allowed_protocol_majors, e.protocol_version)) {
        result.ok = false;
        result.outcome = TransportOutcome::VersionMismatch;
        result.reason = "protocol_version_mismatch";
        return result;
    }

    // 4) messageKind / direction 基础校验
    if (e.message_kind.empty()) {
        result.ok = false;
        result.outcome = TransportOutcome::ProtocolError;
        result.reason = "missing_message_kind";
        return result;
    }

    // 5) service allowlist（首期且当前仅 vehicle.fota；不得接受旧 vehicle.ota）
    if (!containsService(limits_.allowed_services, e.service)) {
        result.ok = false;
        result.outcome = TransportOutcome::Rejected;
        result.reason = "service_not_allowed";
        return result;
    }

    // 6) payloadType 格式/能力校验（fully-qualified，且限定在 service 包前缀）
    const std::string prefix = e.service + ".v1.";
    if (e.payload_type.empty() ||
        e.payload_type.rfind(prefix, 0) != 0 ||
        e.payload_type.size() <= prefix.size() ||
        !std::regex_match(e.payload_type.substr(prefix.size()), kPayloadTypeName)) {
        result.ok = false;
        result.outcome = TransportOutcome::Rejected;
        result.reason = "payload_type_not_allowed";
        return result;
    }

    // 7) timestamp / expireAt（TTL）校验
    const int64_t now_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count();
    if (e.expire_at_ms > 0 && e.expire_at_ms < now_ms) {
        result.ok = false;
        result.outcome = TransportOutcome::Rejected;
        result.reason = "ttl_expired";
        return result;
    }

    // 8) messageId / correlationId 基础校验
    if (e.message_id.empty()) {
        result.ok = false;
        result.outcome = TransportOutcome::ProtocolError;
        result.reason = "missing_message_id";
        return result;
    }

    // 9) payload 大小与资源预算
    if (e.payload.size() > limits_.max_payload_bytes) {
        result.ok = false;
        result.outcome = TransportOutcome::PayloadTooLarge;
        result.reason = "payload_exceeds_max_bytes";
        return result;
    }

    result.ok = true;
    result.outcome = TransportOutcome::Accepted;
    return result;
}

// ============================================================
// 响应 frame wire 编解码
// ============================================================

namespace {

inline void putU16BE(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

inline void putU32BE(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

inline uint16_t getU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

inline uint32_t getU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) |
           static_cast<uint32_t>(p[3]);
}

} // namespace

std::vector<uint8_t> encodeResponseFrame(const VehicleMessageResponseFrame& frame) {
    std::vector<uint8_t> out;
    out.reserve(7 + (frame.response ? frame.response->envelope_bytes.size() : 0));
    out.push_back(static_cast<uint8_t>(frame.outcome));
    putU16BE(out, frame.error ? frame.error->code : 0);
    if (frame.response && !frame.response->envelope_bytes.empty()) {
        putU32BE(out, static_cast<uint32_t>(frame.response->envelope_bytes.size()));
        for (std::byte b : frame.response->envelope_bytes) {
            out.push_back(static_cast<uint8_t>(b));
        }
    } else {
        putU32BE(out, 0);
    }
    return out;
}

VehicleMessageResponseFrame decodeResponseFrame(const std::vector<uint8_t>& wire) {
    VehicleMessageResponseFrame frame;
    if (wire.size() < 7) {
        frame.outcome = TransportOutcome::ProtocolError;
        frame.error = TransportError{static_cast<uint16_t>(ErrorCode::SERIALIZATION_FAILED),
                                     "response_frame_too_short"};
        return frame;
    }
    frame.outcome = static_cast<TransportOutcome>(wire[0]);
    const uint16_t code = getU16BE(wire.data() + 1);
    const uint32_t env_len = getU32BE(wire.data() + 3);
    if (code != 0) {
        frame.error = TransportError{code, ""};
    }
    if (env_len > 0) {
        if (wire.size() < 7u + env_len) {
            frame.outcome = TransportOutcome::ProtocolError;
            frame.error = TransportError{static_cast<uint16_t>(ErrorCode::SERIALIZATION_FAILED),
                                         "response_frame_truncated"};
            return frame;
        }
        VehicleMessageFrame resp;
        resp.envelope_bytes.assign(
            reinterpret_cast<const std::byte*>(wire.data() + 7),
            reinterpret_cast<const std::byte*>(wire.data() + 7 + env_len));
        frame.response = std::move(resp);
    }
    return frame;
}

} // namespace someip
} // namespace tbox
