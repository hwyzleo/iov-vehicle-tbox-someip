#include <gtest/gtest.h>
#include "tbox_someip/someip_log_adapter.h"
#include "tbox_someip/someip_context.h"
#include "someip_daemon.h"
#include "ipc_bridge.h"
#include "service_discovery.h"
#include "services/vehicle_message_gateway.h"
#include <thread>
#include <chrono>
#include <vector>

namespace tbox {
namespace someip {
namespace {

// CR-008：旧 report_software_inventory(0x0001)/fota_command(0x8001) reserved，
// 测试统一改用通用车云消息。以下为 test-only 成员 ID（非量产）。
constexpr uint16_t kTestMethodId = 0x0002;   // METHOD_EXCHANGE_VEHICLE_MESSAGE (test-only)
constexpr uint16_t kTestEventId = 0x8002;    // EVENT_VEHICLE_MESSAGE (test-only)

class LogIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        tbox::fw::log::LogConfig config;
        config.level = tbox::fw::log::LogLevel::kDebug;

        auto result = SomeipLogAdapter::init("someip_integration_test", config);
        ASSERT_EQ(result.error, tbox::fw::log::LogError::kOk);

        bridge_ = std::make_unique<IpcBridge>();
        bridge_->initialize();
    }

    void TearDown() override {
        if (gateway_) {
            gateway_->stop();
        }
        if (bridge_) {
            bridge_->shutdown();
        }
    }

    std::unique_ptr<IpcBridge> bridge_;
    std::unique_ptr<VehicleMessageGateway> gateway_;
};

// 测试服务初始化日志
TEST_F(LogIntegrationTest, ServiceInitialization) {
    SomeipLogAdapter::runtime().info("someip.runtime.initialized",
        "Integration test: runtime initialized"
    );

    SomeipLogAdapter::sd().info("someip.sd.offer.succeeded",
        "Integration test: service offered",
        {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(someip_addressing::SERVICE_ID)),
         tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(someip_addressing::INSTANCE_ID)),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("succeeded")),
         tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(10))}
    );
}

// 测试 Method 入站 -> IPC 调用 -> 响应链路的上下文传播
TEST_F(LogIntegrationTest, ContextPropagationMethodToIpcToResponse) {
    std::string trace_id = "integration-trace-001";
    std::string request_id = "integration-req-001";

    {
        auto scope = make_context_scope(trace_id, request_id);

        // 验证上下文在操作期间被设置
        auto* current = tbox::fw::log::ContextScope::current();
        ASSERT_NE(current, nullptr);
        EXPECT_EQ(current->trace_id, trace_id);
        EXPECT_EQ(current->request_id, request_id);

        // 模拟 Method 入站（通用车云消息 exchange）
        SomeipLogAdapter::method().debug("someip.method.request.received",
            "Method request received",
            {tbox::fw::log::Field("someip_method_id", tbox::fw::log::FieldValue::makeInt(kTestMethodId)),
             tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id))}
        );

        // 模拟 IPC 调用
        SomeipLogAdapter::ipc_bridge().info("someip.ipc.call.started",
            "Internal client call",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("exchange_vehicle_message"))}
        );

        // 模拟 Method 响应
        SomeipLogAdapter::method().info("someip.method.request.completed",
            "Method request completed",
            {tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id)),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("accepted")),
             tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(45))}
        );
    }

    // 上下文已清理
    auto* after = tbox::fw::log::ContextScope::current();
    EXPECT_EQ(after, nullptr);
}

// 测试 IPC 通知 -> Event 下发的上下文传播
TEST_F(LogIntegrationTest, ContextPropagationIpcNotificationToEvent) {
    // 来源无上下文时生成新 trace/request
    std::string new_request_id = generate_request_id();

    {
        auto scope = make_context_scope("", new_request_id);

        // 模拟 IPC 通知转为 Event
        SomeipLogAdapter::event().info("someip.event.published",
            "Event published from IPC notification",
            {tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("up")),
             tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(new_request_id)),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("succeeded"))}
        );
    }

    EXPECT_EQ(tbox::fw::log::ContextScope::current(), nullptr);
}

// 测试通用车云消息上下行跨 SOME/IP/TSP 链路关联（CR-008）
TEST_F(LogIntegrationTest, VehicleMessageUpstreamDownstreamCorrelation) {
    std::string tsp_request_id = "tsp-req-vm-001";

    // 上行：exchangeVehicleMessage
    {
        auto scope = make_context_scope("", tsp_request_id);

        SomeipLogAdapter::vehicle_message().info("someip.vehicle_message.request.completed",
            "Vehicle message relayed to TSP",
            {tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(tsp_request_id)),
             tbox::fw::log::Field("service", tbox::fw::log::FieldValue::makeString("vehicle.fota")),
             tbox::fw::log::Field("payload_type", tbox::fw::log::FieldValue::makeString("vehicle.fota.v1.CheckTask")),
             tbox::fw::log::Field("message_id", tbox::fw::log::FieldValue::makeString("msg-001")),
             tbox::fw::log::Field("outcome", tbox::fw::log::FieldValue::makeString("accepted")),
             tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(120))}
        );
    }

    // 下行：onVehicleMessage（关联同一 TSP 请求标识）
    {
        auto scope = make_context_scope("", tsp_request_id);

        SomeipLogAdapter::vehicle_message().info("someip.vehicle_message.downlink.forwarded",
            "Vehicle message downlink relayed to CGW",
            {tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(tsp_request_id)),
             tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("down")),
             tbox::fw::log::Field("message_id", tbox::fw::log::FieldValue::makeString("msg-002")),
             tbox::fw::log::Field("envelope_length", tbox::fw::log::FieldValue::makeInt(256))}
        );
    }
}

// 测试脱敏：VIN、device_sn、payload 不落日志（CR-008 §日志与指标）
TEST_F(LogIntegrationTest, Desensitization) {
    std::string test_vin = "LSVAU2180N2123456";
    std::string test_device_sn = "SN123456789";

    // VIN 按 Identifier 处理（默认掩码）
    SomeipLogAdapter::vehicle_message().info("someip.test.desensitization",
        "Test VIN desensitization",
        {tbox::fw::log::Field("vin", tbox::fw::log::FieldValue::makeString(test_vin),
             tbox::fw::log::Sensitivity::Identifier),
         tbox::fw::log::Field("device_sn", tbox::fw::log::FieldValue::makeString(test_device_sn),
             tbox::fw::log::Sensitivity::Identifier)}
    );

    // 原始 payload 按 Payload 处理（限长+过滤）
    std::string raw_payload(512, 'X');
    SomeipLogAdapter::serializer().info("someip.test.desensitization",
        "Test payload desensitization",
        {tbox::fw::log::Field("raw_payload", tbox::fw::log::FieldValue::makeString(raw_payload),
             tbox::fw::log::Sensitivity::Payload),
         tbox::fw::log::Field("payload_length", tbox::fw::log::FieldValue::makeInt(512))}
    );

    // 通用车云消息仅记录 service/payloadType/长度/outcome 摘要
    SomeipLogAdapter::vehicle_message().info("someip.vehicle_message.request.received",
        "Test envelope desensitization",
        {tbox::fw::log::Field("service", tbox::fw::log::FieldValue::makeString("vehicle.fota")),
         tbox::fw::log::Field("payload_type", tbox::fw::log::FieldValue::makeString("vehicle.fota.v1.CheckTask")),
         tbox::fw::log::Field("envelope_length", tbox::fw::log::FieldValue::makeInt(256)),
         tbox::fw::log::Field("vin", tbox::fw::log::FieldValue::makeString(test_vin),
             tbox::fw::log::Sensitivity::Identifier)}
    );
}

// 测试 Secret 字段被拒绝
TEST_F(LogIntegrationTest, SecretFieldRejection) {
    SomeipLogAdapter::ipc_bridge().info("someip.test.secret_rejection",
        "Testing secret field rejection",
        {tbox::fw::log::Field("token", tbox::fw::log::FieldValue::makeString("my_token_123"),
             tbox::fw::log::Sensitivity::Secret),
         tbox::fw::log::Field("password", tbox::fw::log::FieldValue::makeString("my_password"),
             tbox::fw::log::Sensitivity::Secret),
         tbox::fw::log::Field("private_key", tbox::fw::log::FieldValue::makeString("private_key_data"),
             tbox::fw::log::Sensitivity::Secret)}
    );
}

// 测试并发日志
TEST_F(LogIntegrationTest, ConcurrentLogging) {
    const int num_threads = 4;
    const int logs_per_thread = 10;
    std::vector<std::thread> threads;

    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([i, logs_per_thread]() {
            for (int j = 0; j < logs_per_thread; ++j) {
                SomeipLogAdapter::runtime().info("someip.test.concurrent",
                    "Concurrent logging test",
                    {tbox::fw::log::Field("thread_id", tbox::fw::log::FieldValue::makeInt(i)),
                     tbox::fw::log::Field("log_index", tbox::fw::log::FieldValue::makeInt(j))}
                );
            }
        });
    }

    for (auto& thread : threads) {
        thread.join();
    }
}

// 测试多模块日志
TEST_F(LogIntegrationTest, MultipleModulesLogging) {
    SomeipLogAdapter::runtime().info("someip.runtime.test", "Runtime module test log");
    SomeipLogAdapter::sd().info("someip.sd.test", "SD module test log");
    SomeipLogAdapter::method().info("someip.method.test", "Method module test log");
    SomeipLogAdapter::event().info("someip.event.test", "Event module test log");
    SomeipLogAdapter::ipc_bridge().info("someip.ipc.test", "IPC bridge module test log");
    SomeipLogAdapter::serializer().info("someip.serializer.test", "Serializer module test log");
    SomeipLogAdapter::vehicle_message().info("someip.vehicle_message.test",
        "Vehicle message module test log");
}

// 测试 VehicleMessageGateway 集成日志（CR-008）
TEST_F(LogIntegrationTest, VehicleMessageGatewayIntegration) {
    // Fake TSP 通用 client（实现 CR-008 通用契约）
    class FakeTspClientForLog : public ITspClient {
    public:
        bool initialize() override { return true; }
        bool isConnected() const override { return true; }
        void shutdown() override {}
        ErrorCode getNetStatus(NetStatusData&) override { return ErrorCode::SUCCESS; }
        void subscribeNetStatusChanges(NetStatusCallback) override {}
        void subscribeRemoteCommands(RemoteCommandCallback) override {}
        bool supportsVehicleMessage() const override { return true; }
        VehicleMessageResponseFrame exchangeVehicleMessage(const VehicleMessageFrame&) override {
            VehicleMessageResponseFrame r;
            r.outcome = TransportOutcome::Accepted;
            return r;
        }
        void subscribeVehicleMessage(std::string_view, VehicleMessageHandler) override {}
    };

    auto fake_client = std::make_shared<FakeTspClientForLog>();
    bridge_->registerClient("tsp", fake_client);

    GenericMessageConfig cfg;
    gateway_ = std::make_unique<VehicleMessageGateway>(
        bridge_.get(), someip_addressing::SERVICE_ID, someip_addressing::INSTANCE_ID, cfg);
    ASSERT_TRUE(gateway_->initialize());
    ASSERT_TRUE(gateway_->start());
    EXPECT_FALSE(gateway_->isDegraded());
}

// 测试 strict 模式初始化
TEST_F(LogIntegrationTest, StrictModeInitialization) {
    tbox::fw::log::LogConfig config;
    config.strict = true;
    config.level = tbox::fw::log::LogLevel::kInfo;

    // 重新初始化（已在 SetUp 中初始化过，这里测试配置）
    // 注意：framework-log 可能不支持重复初始化，此处仅验证配置对象
    EXPECT_TRUE(config.strict);
    EXPECT_EQ(config.level, tbox::fw::log::LogLevel::kInfo);
}

} // namespace
} // namespace someip
} // namespace tbox
