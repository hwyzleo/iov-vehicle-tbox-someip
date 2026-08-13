#pragma once

// 公共 Envelope 解码与准入校验（CR-008 §Envelope 校验管线 / §13.2）。
// SOMEIP 只解析 vehicle.common.v1.VehicleMessageEnvelope（本仓库 proto 生成），
// payload(10) 保持不透明 bytes，只做长度/资源准入；不解析 FOTA 业务字段。

#include "tbox_someip/types.h"
#include "tbox_someip/vehicle_message.h"

#include <string>
#include <vector>

namespace tbox {
namespace someip {

// 校验结果：ok=false 时 outcome 给出稳定传输语义，reason 为受控摘要
struct VehicleMessageValidation {
    bool ok = false;
    TransportOutcome outcome = TransportOutcome::ProtocolError;
    std::string reason;             // 稳定、受控（不记录完整 payload/身份）
    VehicleMessageEnvelope envelope;
};

// 解码 + 准入校验管线：
// IDL/总长度 → proto 解析/初始化 → Envelope major → messageKind/direction →
// service allowlist → payloadType 格式/能力 → timestamp/expireAt → messageId/
// correlationId → payload 大小与资源预算。
class VehicleMessageValidator {
public:
    VehicleMessageValidator() = default;
    explicit VehicleMessageValidator(const VehicleMessageLimits& limits);

    VehicleMessageValidation validate(const VehicleMessageFrame& frame) const;

    const VehicleMessageLimits& limits() const { return limits_; }

    // 工具：transport outcome → 受控错误码摘要（映射 TBOX-SOMEIP-1001~1004）
    static uint16_t outcomeErrorCode(TransportOutcome o);

private:
    VehicleMessageLimits limits_;
};

// ============================================================
// SOME/IP 通用 Method 响应 wire（CR-008 §2 Wire DTO）
// 布局（大端）：
//   [0]      1 byte  TransportOutcome
//   [1..2]   2 bytes error_code（BE；0 = 无错误）
//   [3..6]   4 bytes response envelope 长度（BE；0 = 无业务 RESPONSE）
//   [7..]    response envelope bytes（若长度 > 0）
// 只承载 outcome + envelope_bytes，不承载 error summary（summary 仅入日志）。
std::vector<uint8_t> encodeResponseFrame(const VehicleMessageResponseFrame& frame);
VehicleMessageResponseFrame decodeResponseFrame(const std::vector<uint8_t>& wire);

} // namespace someip
} // namespace tbox
