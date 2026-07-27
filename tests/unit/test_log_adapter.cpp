#include <gtest/gtest.h>
#include "tbox_someip/someip_log_adapter.h"

namespace tbox {
namespace someip {
namespace {

class SomeipLogAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        tbox::fw::log::LogConfig config;
        config.level = tbox::fw::log::LogLevel::kDebug;

        auto result = SomeipLogAdapter::init("someip_test", config);
        ASSERT_EQ(result.error, tbox::fw::log::LogError::kOk);
    }
};

TEST_F(SomeipLogAdapterTest, InitSucceeds) {
    // SetUp 已验证初始化成功
    SUCCEED();
}

TEST_F(SomeipLogAdapterTest, GetRuntimeLogger) {
    auto logger = SomeipLogAdapter::runtime();
    logger.info("test.event", "test message");
}

TEST_F(SomeipLogAdapterTest, GetSdLogger) {
    auto logger = SomeipLogAdapter::sd();
    logger.info("test.event", "test message");
}

TEST_F(SomeipLogAdapterTest, GetMethodLogger) {
    auto logger = SomeipLogAdapter::method();
    logger.info("test.event", "test message");
}

TEST_F(SomeipLogAdapterTest, GetEventLogger) {
    auto logger = SomeipLogAdapter::event();
    logger.info("test.event", "test message");
}

TEST_F(SomeipLogAdapterTest, GetIpcBridgeLogger) {
    auto logger = SomeipLogAdapter::ipc_bridge();
    logger.info("test.event", "test message");
}

TEST_F(SomeipLogAdapterTest, GetSerializerLogger) {
    auto logger = SomeipLogAdapter::serializer();
    logger.info("test.event", "test message");
}

TEST_F(SomeipLogAdapterTest, GetFotaRelayLogger) {
    auto logger = SomeipLogAdapter::fota_relay();
    logger.info("test.event", "test message");
}

TEST_F(SomeipLogAdapterTest, AllLogLevelsWork) {
    auto logger = SomeipLogAdapter::runtime();
    logger.trace("test.trace", "trace message");
    logger.debug("test.debug", "debug message");
    logger.info("test.info", "info message");
    logger.warn("test.warn", "warn message");
    logger.error("test.error", "error message");
}

TEST_F(SomeipLogAdapterTest, LogWithFields) {
    auto logger = SomeipLogAdapter::method();
    logger.info("test.event", "test with fields",
        {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(0x6101)),
         tbox::fw::log::Field("someip_method_id", tbox::fw::log::FieldValue::makeInt(0x0001)),
         tbox::fw::log::Field("someip_client_id", tbox::fw::log::FieldValue::makeInt(0x1234)),
         tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(42))}
    );
}

} // namespace
} // namespace someip
} // namespace tbox
