#include <gtest/gtest.h>
#include "tbox_someip/someip_log_adapter.h"
#include "tbox_someip/someip_context.h"
#include <set>
#include <string>

namespace tbox {
namespace someip {
namespace {

class SomeipContextTest : public ::testing::Test {
protected:
    void SetUp() override {
        tbox::fw::log::LogConfig config;
        config.level = tbox::fw::log::LogLevel::kDebug;

        auto result = SomeipLogAdapter::init("someip_test", config);
        ASSERT_EQ(result.error, tbox::fw::log::LogError::kOk);
    }
};

TEST_F(SomeipContextTest, ContextScopeCreation) {
    std::string trace_id = "trace-abc";
    std::string request_id = "req-123";
    std::string session_id = "session-456";

    {
        auto scope = make_context_scope(trace_id, request_id, session_id);

        // 验证上下文已设置
        auto* current = tbox::fw::log::ContextScope::current();
        ASSERT_NE(current, nullptr);
        EXPECT_EQ(current->trace_id, trace_id);
        EXPECT_EQ(current->request_id, request_id);
        EXPECT_EQ(current->session_id, session_id);
    }

    // 上下文已清理
    auto* after = tbox::fw::log::ContextScope::current();
    EXPECT_EQ(after, nullptr);
}

TEST_F(SomeipContextTest, ContextScopeWithEmptySession) {
    std::string trace_id = "trace-xyz";
    std::string request_id = "req-789";

    {
        auto scope = make_context_scope(trace_id, request_id);

        auto* current = tbox::fw::log::ContextScope::current();
        ASSERT_NE(current, nullptr);
        EXPECT_EQ(current->trace_id, trace_id);
        EXPECT_EQ(current->request_id, request_id);
        EXPECT_TRUE(current->session_id.empty());
    }

    EXPECT_EQ(tbox::fw::log::ContextScope::current(), nullptr);
}

TEST_F(SomeipContextTest, ContextPropagationDuringLogging) {
    std::string trace_id = "trace-method-001";
    std::string request_id = "req-method-001";

    {
        auto scope = make_context_scope(trace_id, request_id);

        // 模拟 Method 入站 -> IPC 调用 -> 响应链路
        SomeipLogAdapter::method().debug("someip.method.request.received",
            "Method request received",
            {tbox::fw::log::Field("someip_method_id", tbox::fw::log::FieldValue::makeInt(0x0001)),
             tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id))}
        );

        // 模拟 IPC 子作用域（继承 trace/request）
        SomeipLogAdapter::ipc_bridge().info("someip.ipc.call.started",
            "Internal client call started",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("report_software_inventory"))}
        );

        // 模拟 Method 处理结束
        SomeipLogAdapter::method().info("someip.method.request.completed",
            "Method request completed",
            {tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id)),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("succeeded")),
             tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(42))}
        );
    }

    // 上下文已清理
    EXPECT_EQ(tbox::fw::log::ContextScope::current(), nullptr);
}

TEST_F(SomeipContextTest, GenerateRequestIdUniqueness) {
    const int count = 100;
    std::set<std::string> ids;

    for (int i = 0; i < count; ++i) {
        ids.insert(generate_request_id());
    }

    // 所有生成的 ID 应唯一
    EXPECT_EQ(ids.size(), count);
}

TEST_F(SomeipContextTest, GenerateRequestIdFormat) {
    std::string id = generate_request_id();

    // 格式: someip-<timestamp>-<random>
    EXPECT_EQ(id.substr(0, 7), "someip-");

    // 应在 "someip-" 后有一个分隔符 '-'（timestamp 与 random 之间）
    size_t first_dash = id.find('-', 7);
    EXPECT_NE(first_dash, std::string::npos);

    // first_dash 之后不应再有额外的 '-'
    size_t second_dash = id.find('-', first_dash + 1);
    EXPECT_EQ(second_dash, std::string::npos);
}

} // namespace
} // namespace someip
} // namespace tbox
