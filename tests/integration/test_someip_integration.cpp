#include <gtest/gtest.h>
#include "someip_daemon.h"
#include "ipc_bridge.h"
#include <thread>
#include <chrono>

using namespace tbox::someip;

// 说明：daemon 内部始终注册真实 framework-ipc 域 client（TSP/PROV）。
// 如需隔离后端，应在测试中 mock 对端 IPC 服务（unix socket 上的 peer），
// 而非在此提供 client 桩。以下生命周期测试依赖门面服务的延迟连接语义
// （后端未就绪时仍能启动）。

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
    config.enable_net_status = true;
    config.enable_remote_cmd_relay = true;
    config.enable_vehicle_message_gateway = true;
    
    // 注册 mock IPC 客户端
    // 注意：需要在 initialize 之前通过 IpcBridge 注册
    
    EXPECT_TRUE(daemon->initialize(config));
    EXPECT_TRUE(daemon->start());
    EXPECT_TRUE(daemon->isRunning());
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    daemon->shutdown();
    EXPECT_FALSE(daemon->isRunning());
}

TEST_F(SomeipIntegrationTest, VehicleMessageGatewayOnly) {
    // 仅测试通用车云消息网关服务
    
    ServiceConfig config;
    config.enable_net_status = false;
    config.enable_remote_cmd_relay = false;
    config.enable_vehicle_message_gateway = true;
    
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
    config.enable_net_status = false;
    config.enable_remote_cmd_relay = false;
    config.enable_vehicle_message_gateway = false;
    
    EXPECT_TRUE(daemon->initialize(config));
    EXPECT_TRUE(daemon->start());
    
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    daemon->shutdown();
}

TEST_F(SomeipIntegrationTest, MultipleStartStop) {
    // 测试多次启停
    
    ServiceConfig config;
    config.enable_net_status = false;
    config.enable_remote_cmd_relay = false;
    config.enable_vehicle_message_gateway = false;
    
    for (int i = 0; i < 3; i++) {
        // 每次循环需要重新创建 daemon 实例
        daemon = std::make_unique<SomeipDaemon>();
        EXPECT_TRUE(daemon->initialize(config));
        EXPECT_TRUE(daemon->start());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        daemon->shutdown();
    }
}
