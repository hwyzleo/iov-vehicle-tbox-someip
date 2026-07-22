#include <gtest/gtest.h>
#include "services/location.h"
#include "ipc_bridge.h"

using namespace tbox::someip;

// 模拟 GNSS 客户端
class MockGnssClient : public IGnssClient {
public:
    bool initialize() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
    ErrorCode getPosition(PositionData& position) override {
        position = {39.9042, 116.4074, 50.0, 60.0, 90.0, true};
        return ErrorCode::SUCCESS;
    }
    void subscribePositionUpdates(PositionCallback callback) override {}
};

class LocationTest : public ::testing::Test {
protected:
    void SetUp() override {
        bridge = std::make_unique<IpcBridge>();
        bridge->initialize();
        
        // 注册模拟 GNSS 客户端
        auto mock_client = std::make_shared<MockGnssClient>();
        bridge->registerClient("gnss", mock_client);
        
        location = std::make_unique<LocationService>(bridge.get());
    }
    
    void TearDown() override {
        if (location) {
            location->stop();
        }
        if (bridge) {
            bridge->shutdown();
        }
    }
    
    std::unique_ptr<IpcBridge> bridge;
    std::unique_ptr<LocationService> location;
};

TEST_F(LocationTest, InitializeSuccess) {
    EXPECT_TRUE(location->initialize());
}

TEST_F(LocationTest, GetPositionSuccess) {
    EXPECT_TRUE(location->initialize());
    EXPECT_TRUE(location->start());
    
    PositionData position;
    EXPECT_EQ(location->getPosition(position), ErrorCode::SUCCESS);
    EXPECT_TRUE(position.is_valid);
}

TEST_F(LocationTest, SubscribePositionUpdates) {
    EXPECT_TRUE(location->initialize());
    EXPECT_TRUE(location->start());
    
    bool received = false;
    location->subscribePositionUpdates([&received](const PositionData&) {
        received = true;
    });
    
    // 订阅不应崩溃
}
