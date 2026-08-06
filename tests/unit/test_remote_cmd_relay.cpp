#include <gtest/gtest.h>
#include "services/remote_cmd_relay.h"
#include "ipc_bridge.h"

using namespace tbox::someip;

// 模拟 TSP 客户端
class MockTspClient : public ITspClient {
public:
    bool initialize() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
    ErrorCode getNetStatus(NetStatusData& status) override {
        return ErrorCode::SUCCESS;
    }
    void subscribeNetStatusChanges(NetStatusCallback callback) override {}
    void subscribeRemoteCommands(RemoteCommandCallback callback) override {}
};

class RemoteCmdRelayTest : public ::testing::Test {
protected:
    void SetUp() override {
        bridge = std::make_unique<IpcBridge>();
        bridge->initialize();
        
        // 注册模拟 TSP 客户端
        auto mock_client = std::make_shared<MockTspClient>();
        bridge->registerClient("tsp", mock_client);
        
        relay = std::make_unique<RemoteCmdRelayService>(bridge.get(), someip_addressing::SERVICE_ID, someip_addressing::INSTANCE_ID);
    }
    
    void TearDown() override {
        if (relay) {
            relay->stop();
        }
        if (bridge) {
            bridge->shutdown();
        }
    }
    
    std::unique_ptr<IpcBridge> bridge;
    std::unique_ptr<RemoteCmdRelayService> relay;
};

TEST_F(RemoteCmdRelayTest, InitializeSuccess) {
    EXPECT_TRUE(relay->initialize());
}

TEST_F(RemoteCmdRelayTest, SubscribeRemoteCommands) {
    EXPECT_TRUE(relay->initialize());
    EXPECT_TRUE(relay->start());
    
    bool received = false;
    relay->subscribeRemoteCommands([&received](const RemoteCommand&) {
        received = true;
    });
    
    // 订阅不应崩溃
}
