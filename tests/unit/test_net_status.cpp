#include <gtest/gtest.h>
#include "services/net_status.h"
#include "ipc_bridge.h"

using namespace tbox::someip;

// 模拟 TSP 客户端
class MockTspClient : public ITspClient {
public:
    bool initialize() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
    ErrorCode getNetStatus(NetStatusData& status) override {
        status.is_online = true;
        status.signal_quality = 85;
        status.network_type = "4G";
        status.is_valid = true;
        return ErrorCode::SUCCESS;
    }
    void subscribeNetStatusChanges(NetStatusCallback callback) override {}
    void subscribeRemoteCommands(RemoteCommandCallback callback) override {}
    bool supportsVehicleMessage() const override { return false; }
    VehicleMessageResponseFrame exchangeVehicleMessage(const VehicleMessageFrame&) override {
        VehicleMessageResponseFrame r;
        r.outcome = TransportOutcome::Unavailable;
        return r;
    }
    void subscribeVehicleMessage(std::string_view, VehicleMessageHandler) override {}
    bool isVehicleMessageDownlinkActive() const override { return false; }
};

class NetStatusTest : public ::testing::Test {
protected:
    void SetUp() override {
        bridge = std::make_unique<IpcBridge>();
        bridge->initialize();
        
        // 注册模拟 TSP 客户端
        auto mock_client = std::make_shared<MockTspClient>();
        bridge->registerClient("tsp", mock_client);
        
        net_status = std::make_unique<NetStatusService>(bridge.get(), someip_addressing::SERVICE_ID, someip_addressing::INSTANCE_ID);
    }
    
    void TearDown() override {
        if (net_status) {
            net_status->stop();
        }
        if (bridge) {
            bridge->shutdown();
        }
    }
    
    std::unique_ptr<IpcBridge> bridge;
    std::unique_ptr<NetStatusService> net_status;
};

TEST_F(NetStatusTest, InitializeSuccess) {
    EXPECT_TRUE(net_status->initialize());
}

TEST_F(NetStatusTest, GetNetStatusSuccess) {
    EXPECT_TRUE(net_status->initialize());
    EXPECT_TRUE(net_status->start());
    
    NetStatusData status;
    EXPECT_EQ(net_status->getNetStatus(status), ErrorCode::SUCCESS);
    EXPECT_TRUE(status.is_valid);
}

TEST_F(NetStatusTest, SubscribeNetStatusChanges) {
    EXPECT_TRUE(net_status->initialize());
    EXPECT_TRUE(net_status->start());
    
    bool received = false;
    net_status->subscribeNetStatusChanges([&received](const NetStatusData&) {
        received = true;
    });
    
    // 订阅不应崩溃
}
