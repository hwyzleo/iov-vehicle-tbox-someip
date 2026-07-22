#include <gtest/gtest.h>
#include "services/time_sync.h"
#include "ipc_bridge.h"

using namespace tbox::someip;

// 模拟 RTC 客户端
class MockRtcClient : public IRtcClient {
public:
    bool initialize() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
    ErrorCode getTime(TimeData& time) override {
        time.timestamp = std::chrono::system_clock::now();
        time.utc_seconds = 1234567890;
        time.utc_offset = 8;
        time.is_valid = true;
        return ErrorCode::SUCCESS;
    }
    void subscribeTimeUpdates(TimeUpdateCallback callback) override {}
};

class TimeSyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        bridge = std::make_unique<IpcBridge>();
        bridge->initialize();
        
        // 注册模拟 RTC 客户端
        auto mock_client = std::make_shared<MockRtcClient>();
        bridge->registerClient("rtc", mock_client);
        
        time_sync = std::make_unique<TimeSyncService>(bridge.get());
    }
    
    void TearDown() override {
        if (time_sync) {
            time_sync->stop();
        }
        if (bridge) {
            bridge->shutdown();
        }
    }
    
    std::unique_ptr<IpcBridge> bridge;
    std::unique_ptr<TimeSyncService> time_sync;
};

TEST_F(TimeSyncTest, InitializeSuccess) {
    EXPECT_TRUE(time_sync->initialize());
}

TEST_F(TimeSyncTest, StartWithoutInit) {
    EXPECT_FALSE(time_sync->start());
}

TEST_F(TimeSyncTest, GetTimeSuccess) {
    EXPECT_TRUE(time_sync->initialize());
    EXPECT_TRUE(time_sync->start());
    
    TimeData time;
    EXPECT_EQ(time_sync->getTime(time), ErrorCode::SUCCESS);
    EXPECT_TRUE(time.is_valid);
}

TEST_F(TimeSyncTest, GetTimeBeforeStart) {
    EXPECT_TRUE(time_sync->initialize());
    
    TimeData time;
    EXPECT_EQ(time_sync->getTime(time), ErrorCode::CLIENT_CALL_FAILED);
    EXPECT_FALSE(time.is_valid);
}

TEST_F(TimeSyncTest, SubscribeTimeUpdates) {
    EXPECT_TRUE(time_sync->initialize());
    EXPECT_TRUE(time_sync->start());
    
    bool received = false;
    time_sync->subscribeTimeUpdates([&received](const TimeData&) {
        received = true;
    });
    
    // 订阅不应崩溃
}
