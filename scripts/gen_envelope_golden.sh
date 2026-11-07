#!/usr/bin/env bash
# =============================================================================
# scripts/gen_envelope_golden.sh
# 重新生成 Envelope golden payload（CR-008 §取代与迁移 / 测试矩阵 9 契约校准）。
#
# 用 CGW-FOTA 仓库实际 proto（canonical SSOT）编译 vehicle.common.v1.Envelope，
# 序列化 REQUEST/RESPONSE/DOWNLINK 三个测试消息并输出 hex。这些 hex 应嵌入
# tests/unit/test_envelope_interop.cpp 的 kGolden*Hex 常量。
#
# 依赖：protoc（>=3.12，proto3 optional）、c++ 编译器、pkg-config protobuf。
# 用法：
#   bash scripts/gen_envelope_golden.sh <CGW-FOTA 仓库根目录> [<输出目录>]
# 输出：stdout 打印三行 GOLDEN_REQUEST/GOLDEN_RESPONSE/GOLDEN_DOWNLINK=hex
# =============================================================================
set -euo pipefail

CGW_ROOT="${1:?usage: gen_envelope_golden.sh <CGW-FOTA repo root> [outdir]}"
OUT_DIR="${2:-$(mktemp -d)}"

PROTO_SRC="${CGW_ROOT}/proto"
ENVELOPE_PROTO="${PROTO_SRC}/vehicle/common/v1/envelope.proto"
if [[ ! -f "${ENVELOPE_PROTO}" ]]; then
    echo "ERROR: canonical envelope.proto not found at ${ENVELOPE_PROTO}" >&2
    exit 1
fi

PROTOC="$(command -v protoc || true)"
if [[ -z "${PROTOC}" ]]; then
    echo "ERROR: protoc not found in PATH" >&2
    exit 1
fi

mkdir -p "${OUT_DIR}/gen" "${OUT_DIR}/src"
"${PROTOC}" --proto_path="${PROTO_SRC}" --cpp_out="${OUT_DIR}/gen" "${ENVELOPE_PROTO}"

cat > "${OUT_DIR}/src/gen_golden.cpp" <<'EOF'
#include "vehicle/common/v1/envelope.pb.h"
#include <cstdio>
#include <string>

static std::string hex(const std::string& b) {
    static const char* h = "0123456789abcdef";
    std::string s;
    for (unsigned char c : b) { s += h[c >> 4]; s += h[c & 0xF]; }
    return s;
}

std::string buildRequest() {
    vehicle::common::v1::VehicleMessageEnvelope env;
    env.set_request_id("req-0001");
    env.set_timestamp_ms(1700000000123LL);
    env.set_protocol_version("fota-v1");
    env.set_device_id("dev-A");
    env.set_vin("VIN12345678901234");
    env.set_vehicle_task_id("vt-1");
    env.set_execution_id("ex-1");
    env.set_idempotency_key("ik-1");
    env.set_payload_type("vehicle.fota.v1.TaskCheckRequest");
    env.set_payload(std::string("\x08\x01\x12\x03""abc", 7));
    env.set_message_id("msg-001");
    env.set_correlation_id("corr-001");
    env.set_message_kind(vehicle::common::v1::MESSAGE_KIND_REQUEST);
    env.set_service("vehicle.fota");
    env.set_expire_at_ms(1700000060123LL);
    env.mutable_trace_context()->set_trace_id("trace-0001");
    env.mutable_trace_context()->set_parent_span_id("span-0001");
    env.mutable_trace_context()->set_trace_flags("01");
    std::string out; if (!env.SerializeToString(&out)) { std::abort(); } return out;
}

std::string buildResponse() {
    vehicle::common::v1::VehicleMessageEnvelope env;
    env.set_request_id("req-0001");
    env.set_timestamp_ms(1700000100000LL);
    env.set_protocol_version("fota-v1");
    env.set_device_id("dev-A");
    env.set_vin("VIN12345678901234");
    env.set_payload_type("vehicle.fota.v1.TaskCheckResponse");
    env.set_payload(std::string("\x08\x01\x10\x00", 4));
    env.set_message_id("msg-901");
    env.set_correlation_id("msg-001");
    env.set_message_kind(vehicle::common::v1::MESSAGE_KIND_RESPONSE);
    env.set_service("vehicle.fota");
    env.set_expire_at_ms(1700000160000LL);
    env.mutable_trace_context()->set_trace_id("trace-0001");
    std::string out; if (!env.SerializeToString(&out)) { std::abort(); } return out;
}

std::string buildDownlink() {
    vehicle::common::v1::VehicleMessageEnvelope env;
    env.set_request_id("req-0002");
    env.set_timestamp_ms(1700000200000LL);
    env.set_protocol_version("fota-v1");
    env.set_device_id("dev-A");
    env.set_vin("VIN12345678901234");
    env.set_payload_type("vehicle.fota.v1.StageResultReport");
    env.set_payload(std::string("\x08\x02", 2));
    env.set_message_id("evt-001");
    env.set_message_kind(vehicle::common::v1::MESSAGE_KIND_EVENT);
    env.set_service("vehicle.fota");
    env.mutable_trace_context()->set_trace_id("trace-0001");
    std::string out; if (!env.SerializeToString(&out)) { std::abort(); } return out;
}

int main() {
    std::string r = buildRequest();
    std::string s = buildResponse();
    std::string d = buildDownlink();
    printf("GOLDEN_REQUEST=%s (%zu bytes)\n", hex(r).c_str(), r.size());
    printf("GOLDEN_RESPONSE=%s (%zu bytes)\n", hex(s).c_str(), s.size());
    printf("GOLDEN_DOWNLINK=%s (%zu bytes)\n", hex(d).c_str(), d.size());
    return 0;
}
EOF

c++ -std=c++20 $(pkg-config --cflags protobuf) -I"${OUT_DIR}/gen" \
    "${OUT_DIR}/src/gen_golden.cpp" "${OUT_DIR}/gen/vehicle/common/v1/envelope.pb.cc" \
    $(pkg-config --libs protobuf) -o "${OUT_DIR}/gen_golden"
"${OUT_DIR}/gen_golden"
