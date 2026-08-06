#include <gtest/gtest.h>
#include "services/fota_relay.h"
#include "ipc_bridge.h"

using namespace tbox::someip;

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
    
    BridgeCallResult reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot) override {
        last_snapshot = snapshot;
        report_called = true;
        return BridgeCallResult{BridgeOutcome::SUCCESS, 0, ""};
    }
    
    void subscribeFotaCommands(FotaCommandCallback callback) override {
        fota_callback = callback;
    }
    
    VehicleSoftwareSnapshot last_snapshot;
    bool report_called = false;
    FotaCommandCallback fota_callback;
};

class FotaRelayTest : public ::testing::Test {
protected:
    void SetUp() override {
        bridge = std::make_unique<IpcBridge>();
        bridge->initialize();
        
        mock_client = std::make_shared<MockTspClient>();
        bridge->registerClient("tsp", mock_client);
        
        relay = std::make_unique<FotaRelayService>(bridge.get(), someip_addressing::SERVICE_ID, someip_addressing::INSTANCE_ID);
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
    std::shared_ptr<MockTspClient> mock_client;
    std::unique_ptr<FotaRelayService> relay;
};

TEST_F(FotaRelayTest, InitializeSuccess) {
    EXPECT_TRUE(relay->initialize());
}

TEST_F(FotaRelayTest, InitializeWithoutBridge) {
    FotaRelayService no_bridge_relay(nullptr, someip_addressing::SERVICE_ID, someip_addressing::INSTANCE_ID);
    EXPECT_FALSE(no_bridge_relay.initialize());
}

TEST_F(FotaRelayTest, GetServiceId) {
    EXPECT_EQ(relay->getServiceId(), 0x6101);
}

TEST_F(FotaRelayTest, GetInstanceId) {
    EXPECT_EQ(relay->getInstanceId(), 0x0001);
}

TEST_F(FotaRelayTest, ReportSoftwareInventorySuccess) {
    ASSERT_TRUE(relay->initialize());
    ASSERT_TRUE(relay->start());
    
    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "TESTVIN1234567890";
    snapshot.registry_version = "1.0.0";
    snapshot.collected_at = "2026-07-23T12:00:00Z";
    snapshot.snapshot_seq = 1;
    
    auto result = relay->reportSoftwareInventory(snapshot);
    EXPECT_EQ(result.outcome, BridgeOutcome::SUCCESS);
    EXPECT_TRUE(mock_client->report_called);
    EXPECT_EQ(mock_client->last_snapshot.vin, "TESTVIN1234567890");
}

TEST_F(FotaRelayTest, ReportSoftwareInventoryWhenStopped) {
    ASSERT_TRUE(relay->initialize());
    
    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "TESTVIN1234567890";
    
    auto result = relay->reportSoftwareInventory(snapshot);
    EXPECT_EQ(result.outcome, BridgeOutcome::FAILED);
}

TEST_F(FotaRelayTest, SubscribeFotaCommands) {
    ASSERT_TRUE(relay->initialize());
    ASSERT_TRUE(relay->start());
    
    bool received = false;
    relay->subscribeFotaCommands([&received](const FotaCommand&) {
        received = true;
    });
    
    // 预留接口，订阅不应崩溃
}

TEST_F(FotaRelayTest, NotifyFotaCommand) {
    ASSERT_TRUE(relay->initialize());
    ASSERT_TRUE(relay->start());
    
    bool received = false;
    relay->subscribeFotaCommands([&received](const FotaCommand& cmd) {
        received = true;
    });
    
    FotaCommand cmd;
    cmd.command_id = "test-001";
    cmd.command_type = "upgrade";
    cmd.payload = "{}";
    
    relay->notifyFotaCommand(cmd);
    EXPECT_TRUE(received);
}
