#include <gtest/gtest.h>
#include "someip_daemon.h"
#include <thread>
#include <chrono>

using namespace tbox::someip;

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
    
    // 1. 初始化
    ServiceConfig config;
    config.enable_time_sync = true;
    config.enable_location = true;
    config.enable_net_status = true;
    config.enable_remote_cmd_relay = true;
    
    EXPECT_TRUE(daemon->initialize(config));
    
    // 2. 启动
    EXPECT_TRUE(daemon->start());
    EXPECT_TRUE(daemon->isRunning());
    
    // 3. 运行一段时间
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 4. 关闭
    daemon->shutdown();
    EXPECT_FALSE(daemon->isRunning());
}

TEST_F(SomeipIntegrationTest, PartialServiceEnable) {
    // 测试部分服务启用
    
    ServiceConfig config;
    config.enable_time_sync = true;
    config.enable_location = false;
    config.enable_net_status = true;
    config.enable_remote_cmd_relay = false;
    
    EXPECT_TRUE(daemon->initialize(config));
    EXPECT_TRUE(daemon->start());
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    daemon->shutdown();
}

TEST_F(SomeipIntegrationTest, ServiceDiscoveryIntegration) {
    // 测试服务发现集成
    
    ServiceConfig config;
    config.enable_sd = true;
    config.sd_offer_interval_ms = 500;
    
    EXPECT_TRUE(daemon->initialize(config));
    EXPECT_TRUE(daemon->start());
    
    // 等待服务发现完成
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    daemon->shutdown();
}

TEST_F(SomeipIntegrationTest, MultipleStartStop) {
    // 测试多次启停
    
    ServiceConfig config;
    
    EXPECT_TRUE(daemon->initialize(config));
    
    for (int i = 0; i < 3; i++) {
        EXPECT_TRUE(daemon->start());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        daemon->shutdown();
    }
}
