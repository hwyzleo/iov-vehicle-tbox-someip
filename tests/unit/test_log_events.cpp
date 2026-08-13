#include <gtest/gtest.h>
#include "tbox_someip/someip_log_adapter.h"
#include "tbox_someip/someip_context.h"
#include "tbox_someip/types.h"
#include <regex>

namespace tbox {
namespace someip {
namespace {

class LogEventsTest : public ::testing::Test {
protected:
    void SetUp() override {
        tbox::fw::log::LogConfig config;
        config.level = tbox::fw::log::LogLevel::kDebug;

        auto result = SomeipLogAdapter::init("someip_test", config);
        ASSERT_EQ(result.error, tbox::fw::log::LogError::kOk);
    }
};

TEST_F(LogEventsTest, EventNameFormat) {
    // 验证事件名格式：someip.<module>.<action> 或 someip.<module>.<submodule>.<action>
    std::regex event_pattern("^someip\\.[a-z_]+\\.[a-z_.]+$");

    // 事件清单中的所有事件（TBOX-SOMEIP-DSN-CR-004 事件目录）
    EXPECT_TRUE(std::regex_match("someip.runtime.initialized", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.sd.offer.succeeded", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.sd.offer.failed", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.sd.subscription.changed", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.method.request.received", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.method.request.completed", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.ipc.call.failed", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.mapping.failed", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.event.published", event_pattern));
    // CR-008 通用车云消息事件（取代旧 someip.fota.*）
    EXPECT_TRUE(std::regex_match("someip.vehicle_message.request.received", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.vehicle_message.request.completed", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.vehicle_message.request.rejected", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.vehicle_message.tsp.accepted", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.vehicle_message.tsp.rejected", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.vehicle_message.tsp.unknown", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.vehicle_message.downlink.received", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.vehicle_message.downlink.forwarded", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.vehicle_message.downlink.dropped", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.vehicle_message.ttl_expired", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.vehicle_message.payload_too_large", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.vehicle_message.correlation_mismatch", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.vehicle_message.queue_full", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.vehicle_message.version_mismatch", event_pattern));
}

TEST_F(LogEventsTest, LoggerMethodsExist) {
    // 验证所有模块 Logger 可以获取并调用
    auto runtime_logger = SomeipLogAdapter::runtime();
    auto sd_logger = SomeipLogAdapter::sd();
    auto method_logger = SomeipLogAdapter::method();
    auto event_logger = SomeipLogAdapter::event();
    auto ipc_bridge_logger = SomeipLogAdapter::ipc_bridge();
    auto serializer_logger = SomeipLogAdapter::serializer();
    auto vehicle_message_logger = SomeipLogAdapter::vehicle_message();

    // 调用日志方法（不崩溃即通过）
    runtime_logger.info("someip.test.event", "test message");
    sd_logger.info("someip.test.event", "test message");
    method_logger.info("someip.test.event", "test message");
    event_logger.info("someip.test.event", "test message");
    ipc_bridge_logger.info("someip.test.event", "test message");
    serializer_logger.info("someip.test.event", "test message");
    vehicle_message_logger.info("someip.test.event", "test message");
}

TEST_F(LogEventsTest, ContextPropagationWorks) {
    std::string trace_id = "trace-123";
    std::string request_id = "req-456";

    {
        auto scope = make_context_scope(trace_id, request_id);

        // 在上下文中记录日志
        SomeipLogAdapter::method().info("someip.method.request.received",
            "Test context propagation",
            {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(0x6101)),
             tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id)),
             tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(100))}
        );
    }

    // 上下文已清理
    auto* current = tbox::fw::log::ContextScope::current();
    EXPECT_EQ(current, nullptr);
}

TEST_F(LogEventsTest, FieldTypesWork) {
    // 验证各种字段类型可以正确传递
    SomeipLogAdapter::runtime().info("someip.test.event",
        "Test field types",
        {tbox::fw::log::Field("string_field", tbox::fw::log::FieldValue::makeString("value")),
         tbox::fw::log::Field("int_field", tbox::fw::log::FieldValue::makeInt(42)),
         tbox::fw::log::Field("bool_field", tbox::fw::log::FieldValue::makeBool(true)),
         tbox::fw::log::Field("double_field", tbox::fw::log::FieldValue::makeDouble(3.14))}
    );
}

TEST_F(LogEventsTest, SensitivityLevelsWork) {
    // 验证不同敏感度级别的字段
    SomeipLogAdapter::vehicle_message().info("someip.test.event",
        "Test sensitivity levels",
        {tbox::fw::log::Field("normal_field",
             tbox::fw::log::FieldValue::makeString("normal"),
             tbox::fw::log::Sensitivity::Normal),
         tbox::fw::log::Field("identifier_field",
             tbox::fw::log::FieldValue::makeString("VIN123456789012345"),
             tbox::fw::log::Sensitivity::Identifier),
         tbox::fw::log::Field("payload_field",
             tbox::fw::log::FieldValue::makeString("payload_data"),
             tbox::fw::log::Sensitivity::Payload),
         tbox::fw::log::Field("secret_field",
             tbox::fw::log::FieldValue::makeString("secret_value"),
             tbox::fw::log::Sensitivity::Secret)}
    );
}

TEST_F(LogEventsTest, GenerateRequestId) {
    std::string id1 = generate_request_id();
    std::string id2 = generate_request_id();

    // 两次生成的 ID 应不同
    EXPECT_NE(id1, id2);

    // ID 应以 "someip-" 开头
    EXPECT_EQ(id1.substr(0, 7), "someip-");
    EXPECT_EQ(id2.substr(0, 7), "someip-");
}

TEST_F(LogEventsTest, ErrorCodeBoundary) {
    // 验证 TBOX-SOMEIP 业务错误码在 1001~1004 范围内
    EXPECT_EQ(static_cast<int>(ErrorCode::NETWORK_NOT_READY), 1001);
    EXPECT_EQ(static_cast<int>(ErrorCode::CLIENT_CALL_TIMEOUT), 1002);
    EXPECT_EQ(static_cast<int>(ErrorCode::CLIENT_CALL_FAILED), 1003);
    EXPECT_EQ(static_cast<int>(ErrorCode::SERIALIZATION_FAILED), 1004);
}

TEST_F(LogEventsTest, SomeipIdFields) {
    // 验证 SOME/IP ID 字段格式（uint16 -> int64）
    SomeipLogAdapter::sd().info("someip.sd.offer.succeeded",
        "Test SOME/IP ID fields",
        {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(someip_addressing::SERVICE_ID)),
         tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(someip_addressing::INSTANCE_ID)),
         tbox::fw::log::Field("port", tbox::fw::log::FieldValue::makeInt(someip_addressing::TCP_PORT)),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("succeeded")),
         tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(15))}
    );
}

TEST_F(LogEventsTest, VehicleMessageEventFields) {
    // 验证通用车云消息事件字段（仅 service/payloadType/长度/outcome 摘要，
    // 不记录完整 Envelope/payload/VIN/凭据——CR-008 §日志与指标）
    SomeipLogAdapter::vehicle_message().info("someip.vehicle_message.request.received",
        "Test vehicle message request",
        {tbox::fw::log::Field("service", tbox::fw::log::FieldValue::makeString("vehicle.fota")),
         tbox::fw::log::Field("payload_type", tbox::fw::log::FieldValue::makeString("vehicle.fota.v1.CheckTask")),
         tbox::fw::log::Field("message_id", tbox::fw::log::FieldValue::makeString("msg-001")),
         tbox::fw::log::Field("envelope_length", tbox::fw::log::FieldValue::makeInt(256)),
         tbox::fw::log::Field("outcome", tbox::fw::log::FieldValue::makeString("accepted"))}
    );

    SomeipLogAdapter::vehicle_message().info("someip.vehicle_message.downlink.forwarded",
        "Test vehicle message downlink",
        {tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("down")),
         tbox::fw::log::Field("message_id", tbox::fw::log::FieldValue::makeString("msg-002")),
         tbox::fw::log::Field("envelope_length", tbox::fw::log::FieldValue::makeInt(128))}
    );
}

TEST_F(LogEventsTest, IpcCallFailedEvent) {
    // 验证 IPC 调用失败事件
    SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
        "Test IPC call failed",
        {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
         tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("exchange_vehicle_message")),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
         tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(5000)),
         tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1002"))}
    );
}

TEST_F(LogEventsTest, MappingFailedEvent) {
    // 验证序列化/映射失败事件
    SomeipLogAdapter::serializer().error("someip.mapping.failed",
        "Test mapping failed",
        {tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("deserialize")),
         tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("up")),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
         tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1004"))}
    );
}

TEST_F(LogEventsTest, FwErrorCodesRange) {
    // 验证 FW-0201~0205 错误码范围
    EXPECT_EQ(static_cast<uint32_t>(tbox::fw::log::LogError::kConfigInvalid), 201);
    EXPECT_EQ(static_cast<uint32_t>(tbox::fw::log::LogError::kInitFailed), 202);
    EXPECT_EQ(static_cast<uint32_t>(tbox::fw::log::LogError::kSinkFailed), 203);
    EXPECT_EQ(static_cast<uint32_t>(tbox::fw::log::LogError::kQueueOverflow), 204);
    EXPECT_EQ(static_cast<uint32_t>(tbox::fw::log::LogError::kSensitiveViolation), 205);
}

} // namespace
} // namespace someip
} // namespace tbox
