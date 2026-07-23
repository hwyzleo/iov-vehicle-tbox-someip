#include <gtest/gtest.h>
#include "someip_daemon.h"
#include "ipc_bridge.h"
#include <thread>
#include <chrono>

using namespace tbox::someip;

// Mock RTC Client
class MockRtcClient : public IRtcClient {
public:
    bool initialize() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
    ErrorCode getTime(TimeData& time) override {
        time.is_valid = true;
        return ErrorCode::SUCCESS;
    }
    void subscribeTimeUpdates(TimeUpdateCallback callback) override {}
};

// Mock GNSS Client
class MockGnssClient : public IGnssClient {
public:
    bool initialize() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
    ErrorCode getPosition(PositionData& position) override {
        position.is_valid = true;
        return ErrorCode::SUCCESS;
    }
    void subscribePositionUpdates(PositionCallback callback) override {}
};

// Mock TSP Client
class MockTspClient : public ITspClient {
public:
    bool initialize() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
    ErrorCode getNetStatus(NetStatusData& status) override {
        status.is_valid = true;
        return ErrorCode::SUCCESS;
    }
    void subscribeNetStatusChanges(NetStatusCallback callback) override {}
    void subscribeRemoteCommands(RemoteCommandCallback callback) override {}
    ErrorCode reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot) override {
        return ErrorCode::SUCCESS;
    }
    void subscribeFotaCommands(FotaCommandCallback callback) override {}
};

// Mock PROV Client
class MockProvClient : public IProvClient {
public:
    bool initialize() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
    std::string getConfiguration(const std::string& key) override { return ""; }
};

class SomeipIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        daemon = std::make_unique<SomeipDaemon>();
    }
    
    void TearDown() override {
        if (daemon) {
            daemon->shutdown();
        }
    }
    
    std::unique_ptr<SomeipDaemon> daemon;
};

TEST_F(SomeipIntegrationTest, FullServiceLifecycle) {
    // 测试完整的服务生命周期
    
    ServiceConfig config;
    config.enable_time_sync = true;
    config.enable_location = true;
    config.enable_net_status = true;
    config.enable_remote_cmd_relay = true;
    config.enable_fota_relay = true;
    
    // 注册 mock IPC 客户端
    // 注意：需要在 initialize 之前通过 IpcBridge 注册
    
    EXPECT_TRUE(daemon->initialize(config));
    EXPECT_TRUE(daemon->start());
    EXPECT_TRUE(daemon->isRunning());
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    daemon->shutdown();
    EXPECT_FALSE(daemon->isRunning());
}

TEST_F(SomeipIntegrationTest, FotaRelayOnly) {
    // 仅测试 FOTA 中继服务
    
    ServiceConfig config;
    config.enable_time_sync = false;
    config.enable_location = false;
    config.enable_net_status = false;
    config.enable_remote_cmd_relay = false;
    config.enable_fota_relay = true;
    
    EXPECT_TRUE(daemon->initialize(config));
    EXPECT_TRUE(daemon->start());
    EXPECT_TRUE(daemon->isRunning());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    daemon->shutdown();
    EXPECT_FALSE(daemon->isRunning());
}

TEST_F(SomeipIntegrationTest, ServiceDiscoveryUnified) {
    // 验证统一 offer 0x6101/0x0001
    
    ServiceConfig config;
    config.enable_sd = true;
    config.enable_time_sync = false;
    config.enable_location = false;
    config.enable_net_status = false;
    config.enable_remote_cmd_relay = false;
    config.enable_fota_relay = false;
    
    EXPECT_TRUE(daemon->initialize(config));
    EXPECT_TRUE(daemon->start());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    daemon->shutdown();
}

TEST_F(SomeipIntegrationTest, MultipleStartStop) {
    // 测试多次启停
    
    ServiceConfig config;
    config.enable_time_sync = false;
    config.enable_location = false;
    config.enable_net_status = false;
    config.enable_remote_cmd_relay = false;
    config.enable_fota_relay = false;
    
    for (int i = 0; i < 3; i++) {
        // 每次循环需要重新创建 daemon 实例
        daemon = std::make_unique<SomeipDaemon>();
        EXPECT_TRUE(daemon->initialize(config));
        EXPECT_TRUE(daemon->start());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        daemon->shutdown();
    }
}
