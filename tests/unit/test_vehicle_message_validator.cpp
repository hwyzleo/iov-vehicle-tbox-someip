// 公共 Envelope 解码与准入校验测试（CR-008 §Envelope 校验管线 / §13.2）。
// 使用本仓库 proto 生成的 VehicleMessageEnvelope 构造合法/非法 wire 样本，
// 验证校验管线各阶段与 TransportOutcome 映射。

#include <gtest/gtest.h>
#include "bridge/vehicle_message_validator.h"

#include "vehicle/common/v1/vehicle_message_envelope.pb.h"

#include <chrono>

using namespace tbox::someip;

namespace {

// 构造 VehicleMessageFrame（用 proto 序列化 Envelope 为 opaque bytes）
VehicleMessageFrame makeFrame(vehicle::common::v1::VehicleMessageEnvelope env) {
    std::string raw;
    env.SerializeToString(&raw);
    VehicleMessageFrame f;
    f.envelope_bytes.resize(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        f.envelope_bytes[i] = static_cast<std::byte>(raw[i]);
    }
    return f;
}

// 构造合法 Envelope（默认 vehicle.fota / CheckTask / 未来 TTL / 小 payload）
vehicle::common::v1::VehicleMessageEnvelope makeValidEnvelope() {
    vehicle::common::v1::VehicleMessageEnvelope env;
    env.set_request_id(1);
    env.set_timestamp_ms(1000);
    env.set_protocol_version(1);
    env.set_service("vehicle.fota");
    env.set_payload_type("vehicle.fota.v1.CheckTask");
    env.set_payload("{\"task_id\":\"t-1\"}");
    env.set_message_id("msg-001");
    env.set_correlation_id("");
    env.set_message_kind("REQUEST");
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    env.set_expire_at_ms(now + 60000);
    return env;
}

VehicleMessageLimits defaultLimits() {
    VehicleMessageLimits l;
    l.allowed_services = {"vehicle.fota"};
    l.allowed_protocol_majors = {1};
    l.max_envelope_bytes = 16384;
    l.max_payload_bytes = 8192;
    return l;
}

} // namespace

TEST(VehicleMessageValidatorTest, ValidEnvelopeAccepted) {
    VehicleMessageValidator v(defaultLimits());
    auto r = v.validate(makeFrame(makeValidEnvelope()));
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.outcome, TransportOutcome::Accepted);
    EXPECT_EQ(r.envelope.service, "vehicle.fota");
    EXPECT_EQ(r.envelope.payload_type, "vehicle.fota.v1.CheckTask");
    EXPECT_EQ(r.envelope.message_id, "msg-001");
    EXPECT_GT(r.envelope.serialized_length, 0u);
}

TEST(VehicleMessageValidatorTest, EmptyEnvelopeRejected) {
    VehicleMessageValidator v(defaultLimits());
    VehicleMessageFrame f;  // empty bytes
    auto r = v.validate(f);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.outcome, TransportOutcome::ProtocolError);
    EXPECT_EQ(r.reason, "empty_envelope");
}

TEST(VehicleMessageValidatorTest, GarbageBytesProtocolError) {
    VehicleMessageValidator v(defaultLimits());
    VehicleMessageFrame f;
    f.envelope_bytes = {std::byte{0xff}, std::byte{0x00}, std::byte{0x11}};
    auto r = v.validate(f);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.outcome, TransportOutcome::ProtocolError);
}

TEST(VehicleMessageValidatorTest, ServiceNotAllowedRejected) {
    auto env = makeValidEnvelope();
    env.set_service("vehicle.ota");  // 旧别名不得接受（US-012）
    VehicleMessageValidator v(defaultLimits());
    auto r = v.validate(makeFrame(env));
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.outcome, TransportOutcome::Rejected);
    EXPECT_EQ(r.reason, "service_not_allowed");
}

TEST(VehicleMessageValidatorTest, PayloadTypeNotFullyQualifiedRejected) {
    auto env = makeValidEnvelope();
    env.set_payload_type("CheckTask");  // 非 FQN
    VehicleMessageValidator v(defaultLimits());
    auto r = v.validate(makeFrame(env));
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.outcome, TransportOutcome::Rejected);
    EXPECT_EQ(r.reason, "payload_type_not_allowed");
}

TEST(VehicleMessageValidatorTest, PayloadTypeWrongServicePrefixRejected) {
    auto env = makeValidEnvelope();
    env.set_payload_type("vehicle.diag.v1.Something");  // 非 vehicle.fota 前缀
    VehicleMessageValidator v(defaultLimits());
    auto r = v.validate(makeFrame(env));
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.outcome, TransportOutcome::Rejected);
}

TEST(VehicleMessageValidatorTest, ProtocolMajorMismatch) {
    auto env = makeValidEnvelope();
    env.set_protocol_version(2);  // 仅允许 major=1
    VehicleMessageValidator v(defaultLimits());
    auto r = v.validate(makeFrame(env));
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.outcome, TransportOutcome::VersionMismatch);
}

TEST(VehicleMessageValidatorTest, TtlExpiredRejected) {
    auto env = makeValidEnvelope();
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    env.set_expire_at_ms(now - 1000);  // 已过期
    VehicleMessageValidator v(defaultLimits());
    auto r = v.validate(makeFrame(env));
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.outcome, TransportOutcome::Rejected);
    EXPECT_EQ(r.reason, "ttl_expired");
}

TEST(VehicleMessageValidatorTest, MissingMessageIdProtocolError) {
    auto env = makeValidEnvelope();
    env.set_message_id("");
    VehicleMessageValidator v(defaultLimits());
    auto r = v.validate(makeFrame(env));
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.outcome, TransportOutcome::ProtocolError);
    EXPECT_EQ(r.reason, "missing_message_id");
}

TEST(VehicleMessageValidatorTest, MissingMessageKindProtocolError) {
    auto env = makeValidEnvelope();
    env.set_message_kind("");
    VehicleMessageValidator v(defaultLimits());
    auto r = v.validate(makeFrame(env));
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.outcome, TransportOutcome::ProtocolError);
    EXPECT_EQ(r.reason, "missing_message_kind");
}

TEST(VehicleMessageValidatorTest, PayloadTooLarge) {
    auto env = makeValidEnvelope();
    env.set_payload(std::string(8193, 'x'));  // 超 max_payload_bytes
    VehicleMessageValidator v(defaultLimits());
    auto r = v.validate(makeFrame(env));
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.outcome, TransportOutcome::PayloadTooLarge);
    EXPECT_EQ(r.reason, "payload_exceeds_max_bytes");
}

TEST(VehicleMessageValidatorTest, EnvelopeTooLarge) {
    // 超 max_envelope_bytes：总长直接超限（不截断、不分片、不落盘）
    VehicleMessageLimits l = defaultLimits();
    l.max_envelope_bytes = 16;
    VehicleMessageValidator v(l);
    auto r = v.validate(makeFrame(makeValidEnvelope()));  // 序列化后 > 16
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(r.outcome, TransportOutcome::PayloadTooLarge);
}

TEST(VehicleMessageValidatorTest, OutcomeErrorCodeMapping) {
    EXPECT_EQ(VehicleMessageValidator::outcomeErrorCode(TransportOutcome::Accepted), 0u);
    EXPECT_EQ(VehicleMessageValidator::outcomeErrorCode(TransportOutcome::Timeout),
              static_cast<uint16_t>(ErrorCode::CLIENT_CALL_TIMEOUT));       // 1002
    EXPECT_EQ(VehicleMessageValidator::outcomeErrorCode(TransportOutcome::Unavailable),
              static_cast<uint16_t>(ErrorCode::CLIENT_CALL_FAILED));        // 1003
    EXPECT_EQ(VehicleMessageValidator::outcomeErrorCode(TransportOutcome::ProtocolError),
              static_cast<uint16_t>(ErrorCode::SERIALIZATION_FAILED));      // 1004
}

// ---- 响应 frame wire 编解码 ----

TEST(VehicleMessageValidatorTest, EncodeDecodeResponseFrameAcceptedNoResponse) {
    VehicleMessageResponseFrame f;
    f.outcome = TransportOutcome::Accepted;
    auto wire = encodeResponseFrame(f);
    auto back = decodeResponseFrame(wire);
    EXPECT_EQ(back.outcome, TransportOutcome::Accepted);
    EXPECT_FALSE(back.error.has_value());
    EXPECT_FALSE(back.response.has_value());
}

TEST(VehicleMessageValidatorTest, EncodeDecodeResponseFrameWithResponseEnvelope) {
    VehicleMessageResponseFrame f;
    f.outcome = TransportOutcome::Accepted;
    VehicleMessageFrame env;
    env.envelope_bytes = {std::byte{0x0a}, std::byte{0x02}, std::byte{0x08}};
    f.response = env;

    auto wire = encodeResponseFrame(f);
    auto back = decodeResponseFrame(wire);
    EXPECT_EQ(back.outcome, TransportOutcome::Accepted);
    ASSERT_TRUE(back.response.has_value());
    ASSERT_EQ(back.response->envelope_bytes.size(), 3u);
    EXPECT_EQ(back.response->envelope_bytes[0], std::byte{0x0a});
}

TEST(VehicleMessageValidatorTest, EncodeDecodeResponseFrameWithError) {
    VehicleMessageResponseFrame f;
    f.outcome = TransportOutcome::ProtocolError;
    f.error = TransportError{static_cast<uint16_t>(ErrorCode::SERIALIZATION_FAILED), "bad"};
    auto wire = encodeResponseFrame(f);
    auto back = decodeResponseFrame(wire);
    EXPECT_EQ(back.outcome, TransportOutcome::ProtocolError);
    ASSERT_TRUE(back.error.has_value());
    EXPECT_EQ(back.error->code, static_cast<uint16_t>(ErrorCode::SERIALIZATION_FAILED));
    EXPECT_FALSE(back.response.has_value());
}

TEST(VehicleMessageValidatorTest, DecodeTruncatedResponseFrame) {
    std::vector<uint8_t> wire = {0x00, 0x00, 0x00};  // 长度 < 7
    auto back = decodeResponseFrame(wire);
    EXPECT_EQ(back.outcome, TransportOutcome::ProtocolError);
}
