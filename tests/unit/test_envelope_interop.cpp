// =============================================================================
// tests/unit/test_envelope_interop.cpp
// 公共 Envelope 契约互操作测试（CR-008 §取代与迁移 / §测试矩阵 9，契约校准 2026-08-17）
// =============================================================================
// 目的：证明 TBOX-SOMEIP 本地 proto（proto/vehicle/common/v1/
// vehicle_message_envelope.proto）与 VEH-PROTO canonical（CGW-FOTA-DSN-CR-011 /
// TBOX-TSP-DSN-CR-009 / iov-cloud-proto-vehicle-common 同源 envelope.proto）
// 处于同一 release-set，wire 层互通：
//
//   1) descriptor 契约测试：断言本地编译 descriptor 与 canonical 字段表逐项一致
//      （字段号/名称/类型/label/presence、MessageKind 枚举值、TraceContext 子字段）。
//   2) golden payload 互操作：用 CGW-FOTA 仓库实际 envelope.proto 生成的 canonical
//      序列化字节（REQUEST/RESPONSE/DOWNLINK 三组 golden）解码，断言全部字段，
//      再重编码并断言字节级一致——本地生成的 wire 与 CGW-FOTA 逐字节相同。
//
// golden 字节重生成：scripts/gen_envelope_golden.sh（用 protoc 编译 CGW-FOTA
// proto 后序列化输出 hex）；任何字段类型/枚举/编号变更必须与同 release-set 的
// CGW-FOTA / TSP / 云端 / 本 golden 原子升级，否则本测试失败。
// =============================================================================

#include <gtest/gtest.h>

#include "vehicle/common/v1/vehicle_message_envelope.pb.h"

#include <google/protobuf/descriptor.h>

#include <cstdint>
#include <string>
#include <vector>

using google::protobuf::FieldDescriptor;

namespace {

std::vector<std::byte> bytesFromHex(const std::string& hex) {
    std::vector<std::byte> out;
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<std::byte>(std::stoul(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

std::string bytesToStr(const std::vector<std::byte>& b) {
    std::string s(b.size(), '\0');
    for (size_t i = 0; i < b.size(); ++i) s[i] = static_cast<char>(b[i]);
    return s;
}

// ---------------------------------------------------------------------------
// 1) descriptor 契约：canonical 字段表（与 VEH-PROTO/CGW-FOTA envelope.proto
//    逐项一致）。此表是本仓库 SSOT 快照，漂移即失败。
// ---------------------------------------------------------------------------
struct CanonicalField {
    int number;
    const char* name;
    FieldDescriptor::Type type;
    bool has_presence;              // proto3 optional / message 的可选语义
    const char* type_name;          // enum/message 的全限定名（标量留空）
};

const CanonicalField kCanonicalEnvelopeFields[] = {
    {1,  "request_id",       FieldDescriptor::TYPE_STRING, false, ""},
    {2,  "timestamp_ms",     FieldDescriptor::TYPE_INT64,  false, ""},
    {3,  "protocol_version", FieldDescriptor::TYPE_STRING, false, ""},
    {4,  "device_id",        FieldDescriptor::TYPE_STRING, false, ""},
    {5,  "vin",              FieldDescriptor::TYPE_STRING, false, ""},
    {6,  "vehicle_task_id",  FieldDescriptor::TYPE_STRING, true,  ""},
    {7,  "execution_id",     FieldDescriptor::TYPE_STRING, true,  ""},
    {8,  "idempotency_key",  FieldDescriptor::TYPE_STRING, true,  ""},
    {9,  "payload_type",     FieldDescriptor::TYPE_STRING, false, ""},
    {10, "payload",          FieldDescriptor::TYPE_BYTES,  false, ""},
    {11, "message_id",       FieldDescriptor::TYPE_STRING, false, ""},
    {12, "correlation_id",   FieldDescriptor::TYPE_STRING, true,  ""},
    {13, "message_kind",     FieldDescriptor::TYPE_ENUM,   false,
         "vehicle.common.v1.MessageKind"},
    {14, "service",          FieldDescriptor::TYPE_STRING, false, ""},
    {15, "expire_at_ms",     FieldDescriptor::TYPE_INT64,  true,  ""},
    {16, "trace_context",    FieldDescriptor::TYPE_MESSAGE, true,
         "vehicle.common.v1.TraceContext"},
};

const struct {
    int number;
    const char* name;
    const char* type_name;
    int value;
} kCanonicalMessageKindValues[] = {
    {0, "MESSAGE_KIND_UNSPECIFIED", "vehicle.common.v1.MessageKind", 0},
    {1, "MESSAGE_KIND_REQUEST",     "vehicle.common.v1.MessageKind", 1},
    {2, "MESSAGE_KIND_RESPONSE",    "vehicle.common.v1.MessageKind", 2},
    {3, "MESSAGE_KIND_EVENT",       "vehicle.common.v1.MessageKind", 3},
};

const struct {
    int number;
    const char* name;
    FieldDescriptor::Type type;
    bool has_presence;
} kCanonicalTraceContextFields[] = {
    {1, "trace_id",       FieldDescriptor::TYPE_STRING, false},
    {2, "parent_span_id", FieldDescriptor::TYPE_STRING, true},
    {3, "trace_flags",    FieldDescriptor::TYPE_STRING, true},
};

// ---------------------------------------------------------------------------
// 2) golden payload（CGW-FOTA 仓库 envelope.proto 实际生成，见文件头注释）
// ---------------------------------------------------------------------------
const char kGoldenRequestHex[] =
    "0a087265712d3030303110fbd095ffbc311a07666f74612d763122056465762d412a1456494e31"
    "32333435363738393031323334353637320476742d313a0465782d314204696b2d314a2076656869"
    "636c652e666f74612e76312e5461736b436865636b526571756573745207080112036162635a076d"
    "73672d3030316208636f72722d3030316801720c76656869636c652e666f746178dba599ffbc3182"
    "011b0a0a74726163652d3030303112097370616e2d303030311a023031";
const char kGoldenResponseHex[] =
    "0a087265712d3030303110a0dd9bffbc311a07666f74612d763122056465762d412a1456494e3132"
    "3334353637383930313233343536374a2176656869636c652e666f74612e76312e5461736b436865"
    "636b526573706f6e73655204080110005a076d73672d39303162076d73672d3030316802720c7665"
    "6869636c652e666f74617880b29fffbc3182010c0a0a74726163652d30303031";
const char kGoldenDownlinkHex[] =
    "0a087265712d3030303210c0eaa1ffbc311a07666f74612d763122056465762d412a1456494e3132"
    "3334353637383930313233343536374a2176656869636c652e666f74612e76312e53746167655265"
    "73756c745265706f7274520208025a076576742d3030316803720c76656869636c652e666f746182"
    "010c0a0a74726163652d30303031";

} // namespace

// ===========================================================================
// descriptor 契约测试
// ===========================================================================
TEST(EnvelopeInteropTest, EnvelopeDescriptorMatchesCanonical) {
    const google::protobuf::Descriptor* d =
        vehicle::common::v1::VehicleMessageEnvelope::descriptor();
    ASSERT_NE(d, nullptr);
    ASSERT_EQ(d->field_count(), 16);

    for (const auto& c : kCanonicalEnvelopeFields) {
        SCOPED_TRACE("field " + std::to_string(c.number) + " " + c.name);
        const FieldDescriptor* f = d->FindFieldByNumber(c.number);
        ASSERT_NE(f, nullptr);
        EXPECT_EQ(f->name(), c.name);
        EXPECT_EQ(f->type(), c.type);
        EXPECT_FALSE(f->is_repeated());
        // presence：proto3 optional / message 字段须与 canonical 一致，避免
        // 隐性默认值污染准入判断（契约校准）
        EXPECT_EQ(f->has_presence(), c.has_presence);
        if (c.type == FieldDescriptor::TYPE_ENUM) {
            ASSERT_NE(f->enum_type(), nullptr) << "field must be enum";
            EXPECT_EQ(f->enum_type()->full_name(), c.type_name);
        } else if (c.type == FieldDescriptor::TYPE_MESSAGE) {
            ASSERT_NE(f->message_type(), nullptr) << "field must be message";
            EXPECT_EQ(f->message_type()->full_name(), c.type_name);
        } else {
            EXPECT_EQ(f->enum_type(), nullptr);
            EXPECT_EQ(f->message_type(), nullptr);
        }
    }
}

TEST(EnvelopeInteropTest, MessageKindEnumMatchesCanonical) {
    const google::protobuf::EnumDescriptor* e =
        vehicle::common::v1::MessageKind_descriptor();
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(e->value_count(), 4);
    for (const auto& v : kCanonicalMessageKindValues) {
        SCOPED_TRACE(v.name);
        const google::protobuf::EnumValueDescriptor* ev = e->FindValueByNumber(v.value);
        ASSERT_NE(ev, nullptr);
        EXPECT_EQ(ev->name(), v.name);
    }
}

TEST(EnvelopeInteropTest, TraceContextMessageMatchesCanonical) {
    const google::protobuf::Descriptor* d =
        vehicle::common::v1::TraceContext::descriptor();
    ASSERT_NE(d, nullptr);
    ASSERT_EQ(d->field_count(), 3);
    for (const auto& c : kCanonicalTraceContextFields) {
        SCOPED_TRACE("field " + std::to_string(c.number) + " " + c.name);
        const FieldDescriptor* f = d->FindFieldByNumber(c.number);
        ASSERT_NE(f, nullptr);
        EXPECT_EQ(f->name(), c.name);
        EXPECT_EQ(f->type(), c.type);
        EXPECT_EQ(f->has_presence(), c.has_presence);
    }
}

// ===========================================================================
// golden payload 互操作：CGW-FOTA canonical 字节 ↔ 本地生成代码
// ===========================================================================
TEST(EnvelopeInteropTest, GoldenRequestDecodesAndRoundTrips) {
    vehicle::common::v1::VehicleMessageEnvelope env;
    ASSERT_TRUE(env.ParseFromString(bytesToStr(bytesFromHex(kGoldenRequestHex))));

    // 全字段断言（canonical 语义）
    EXPECT_EQ(env.request_id(), "req-0001");
    EXPECT_EQ(env.timestamp_ms(), 1700000000123LL);
    EXPECT_EQ(env.protocol_version(), "fota-v1");
    EXPECT_EQ(env.device_id(), "dev-A");
    EXPECT_EQ(env.vin(), "VIN12345678901234567");
    EXPECT_TRUE(env.has_vehicle_task_id());
    EXPECT_EQ(env.vehicle_task_id(), "vt-1");
    EXPECT_TRUE(env.has_execution_id());
    EXPECT_EQ(env.execution_id(), "ex-1");
    EXPECT_TRUE(env.has_idempotency_key());
    EXPECT_EQ(env.idempotency_key(), "ik-1");
    EXPECT_EQ(env.payload_type(), "vehicle.fota.v1.TaskCheckRequest");
    EXPECT_EQ(env.payload(), std::string("\x08\x01\x12\x03""abc", 7));
    EXPECT_EQ(env.message_id(), "msg-001");
    EXPECT_TRUE(env.has_correlation_id());
    EXPECT_EQ(env.correlation_id(), "corr-001");
    EXPECT_EQ(env.message_kind(), vehicle::common::v1::MESSAGE_KIND_REQUEST);
    EXPECT_EQ(env.service(), "vehicle.fota");
    EXPECT_TRUE(env.has_expire_at_ms());
    EXPECT_EQ(env.expire_at_ms(), 1700000060123LL);
    ASSERT_TRUE(env.has_trace_context());
    EXPECT_EQ(env.trace_context().trace_id(), "trace-0001");
    EXPECT_EQ(env.trace_context().parent_span_id(), "span-0001");
    EXPECT_EQ(env.trace_context().trace_flags(), "01");

    // 重编码字节级一致（与 CGW-FOTA 同 release-set 的 wire 互通）
    std::string re;
    ASSERT_TRUE(env.SerializeToString(&re));
    EXPECT_EQ(bytesToStr(bytesFromHex(kGoldenRequestHex)), re);
}

TEST(EnvelopeInteropTest, GoldenResponseDecodesAndRoundTrips) {
    vehicle::common::v1::VehicleMessageEnvelope env;
    ASSERT_TRUE(env.ParseFromString(bytesToStr(bytesFromHex(kGoldenResponseHex))));

    EXPECT_EQ(env.request_id(), "req-0001");
    EXPECT_EQ(env.protocol_version(), "fota-v1");
    EXPECT_EQ(env.payload_type(), "vehicle.fota.v1.TaskCheckResponse");
    EXPECT_EQ(env.payload(), std::string("\x08\x01\x10\x00", 4));
    EXPECT_EQ(env.message_id(), "msg-901");
    EXPECT_TRUE(env.has_correlation_id());
    EXPECT_EQ(env.correlation_id(), "msg-001");   // 指向请求 message_id
    EXPECT_EQ(env.message_kind(), vehicle::common::v1::MESSAGE_KIND_RESPONSE);
    EXPECT_EQ(env.service(), "vehicle.fota");
    EXPECT_TRUE(env.has_trace_context());
    EXPECT_EQ(env.trace_context().trace_id(), "trace-0001");

    std::string re;
    ASSERT_TRUE(env.SerializeToString(&re));
    EXPECT_EQ(bytesToStr(bytesFromHex(kGoldenResponseHex)), re);
}

TEST(EnvelopeInteropTest, GoldenDownlinkDecodesAndRoundTrips) {
    vehicle::common::v1::VehicleMessageEnvelope env;
    ASSERT_TRUE(env.ParseFromString(bytesToStr(bytesFromHex(kGoldenDownlinkHex))));

    EXPECT_EQ(env.request_id(), "req-0002");
    EXPECT_EQ(env.protocol_version(), "fota-v1");
    EXPECT_EQ(env.payload_type(), "vehicle.fota.v1.StageResultReport");
    EXPECT_EQ(env.payload(), std::string("\x08\x02", 2));
    EXPECT_EQ(env.message_id(), "evt-001");
    EXPECT_EQ(env.message_kind(), vehicle::common::v1::MESSAGE_KIND_EVENT);
    EXPECT_EQ(env.service(), "vehicle.fota");
    EXPECT_FALSE(env.has_correlation_id());       // EVENT 未显式关联
    EXPECT_FALSE(env.has_expire_at_ms());         // optional 未填 = 无 TTL
    ASSERT_TRUE(env.has_trace_context());
    EXPECT_EQ(env.trace_context().trace_id(), "trace-0001");

    std::string re;
    ASSERT_TRUE(env.SerializeToString(&re));
    EXPECT_EQ(bytesToStr(bytesFromHex(kGoldenDownlinkHex)), re);
}
