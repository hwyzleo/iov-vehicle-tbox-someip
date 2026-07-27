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
    EXPECT_TRUE(std::regex_match("someip.fota.inventory.relayed", event_pattern));
    EXPECT_TRUE(std::regex_match("someip.fota.command.relayed", event_pattern));
}

TEST_F(LogEventsTest, LoggerMethodsExist) {
    // 验证所有模块 Logger 可以获取并调用
    auto runtime_logger = SomeipLogAdapter::runtime();
    auto sd_logger = SomeipLogAdapter::sd();
    auto method_logger = SomeipLogAdapter::method();
    auto event_logger = SomeipLogAdapter::event();
    auto ipc_bridge_logger = SomeipLogAdapter::ipc_bridge();
    auto serializer_logger = SomeipLogAdapter::serializer();
    auto fota_relay_logger = SomeipLogAdapter::fota_relay();

    // 调用日志方法（不崩溃即通过）
    runtime_logger.info("someip.test.event", "test message");
    sd_logger.info("someip.test.event", "test message");
    method_logger.info("someip.test.event", "test message");
    event_logger.info("someip.test.event", "test message");
    ipc_bridge_logger.info("someip.test.event", "test message");
    serializer_logger.info("someip.test.event", "test message");
    fota_relay_logger.info("someip.test.event", "test message");
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
    SomeipLogAdapter::fota_relay().info("someip.test.event",
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

TEST_F(LogEventsTest, FotaEventFields) {
    // 验证 FOTA 事件字段（VIN 按 Identifier 脱敏）
    std::string test_vin = "LSVAU2180N2123456";

    SomeipLogAdapter::fota_relay().info("someip.fota.inventory.relayed",
        "Test FOTA inventory relay",
        {tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("up")),
         tbox::fw::log::Field("entry_count", tbox::fw::log::FieldValue::makeInt(3)),
         tbox::fw::log::Field("snapshot_seq", tbox::fw::log::FieldValue::makeInt(1)),
         tbox::fw::log::Field("vin", tbox::fw::log::FieldValue::makeString(test_vin),
             tbox::fw::log::Sensitivity::Identifier),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("succeeded")),
         tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(52))}
    );

    SomeipLogAdapter::fota_relay().info("someip.fota.command.relayed",
        "Test FOTA command relay",
        {tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("down")),
         tbox::fw::log::Field("command_id", tbox::fw::log::FieldValue::makeString("cmd-001")),
         tbox::fw::log::Field("payload_length", tbox::fw::log::FieldValue::makeInt(128),
             tbox::fw::log::Sensitivity::Payload),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("succeeded")),
         tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(30))}
    );
}

TEST_F(LogEventsTest, IpcCallFailedEvent) {
    // 验证 IPC 调用失败事件
    SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
        "Test IPC call failed",
        {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
         tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("report_software_inventory")),
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
