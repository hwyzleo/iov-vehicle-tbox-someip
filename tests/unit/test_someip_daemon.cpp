#include <gtest/gtest.h>
#include "someip_daemon.h"

using namespace tbox::someip;

class SomeipDaemonTest : public ::testing::Test {
protected:
    void SetUp() override {
        daemon = std::make_unique<SomeipDaemon>();
    }
    
    void TearDown() override {
        if (daemon) {
            daemon->shutdown();
        }
    }

    // CR-008 契约 B 测试只需通用车云消息网关；关闭 NetStatus/RemoteCmd
    // （其 worker 5s 轮询会拉长 shutdown join，与本测试无关）。
    ServiceConfig contractBConfig() {
        ServiceConfig config;
        config.enable_net_status = false;
        config.enable_remote_cmd_relay = false;
        config.enable_vehicle_message_gateway = true;
        return config;
    }
    
    std::unique_ptr<SomeipDaemon> daemon;
};

TEST_F(SomeipDaemonTest, InitializeSuccess) {
    EXPECT_TRUE(daemon->initialize());
}

TEST_F(SomeipDaemonTest, StartWithoutInit) {
    EXPECT_FALSE(daemon->start());
}

TEST_F(SomeipDaemonTest, Shutdown) {
    EXPECT_TRUE(daemon->initialize(contractBConfig()));
    EXPECT_TRUE(daemon->start());
    
    // 关闭不应崩溃
    daemon->shutdown();
}

// ---- CR-008/US-011 通用成员注册链（fail-closed）：精确 ID 由 Registry 分配 ----
// 通过 SomeipDaemon::serviceDiscovery() 只读查询 mock SomeIpService 注册状态。

TEST_F(SomeipDaemonTest, ContractBZeroRegistrationWhenIdsMissing) {
    // 默认 config：method/event/eventgroup 均未分配（nullopt）→ 零注册、零 Offer Event
    ServiceConfig config = contractBConfig();
    ASSERT_TRUE(daemon->initialize(config));
    ASSERT_TRUE(daemon->start());
    auto* sd = daemon->serviceDiscovery();
    ASSERT_NE(sd, nullptr);
    // 未分配：Method/Event/Eventgroup 零注册（不猜测未批准数值，fail-closed）
    EXPECT_FALSE(sd->hasMethodHandler(0x0002));   // dev test-only ID 未分配
    EXPECT_FALSE(sd->hasEventHandler(0x8002));
    EXPECT_FALSE(sd->hasEventGroup(0x8002));
}

TEST_F(SomeipDaemonTest, ContractBAllRegisteredWhenIdsComplete) {
    ServiceConfig config = contractBConfig();
    config.method_exchange_vehicle_message_id = 0x0002;   // TEST-ONLY（非量产）
    config.event_vehicle_message_id = 0x8002;             // TEST-ONLY
    config.eventgroup_id = 7;
    ASSERT_TRUE(daemon->initialize(config));
    ASSERT_TRUE(daemon->start());
    auto* sd = daemon->serviceDiscovery();
    ASSERT_NE(sd, nullptr);
    // ID 完整：Method/Event/Eventgroup 均注册
    EXPECT_TRUE(sd->hasMethodHandler(0x0002));
    EXPECT_TRUE(sd->hasEventHandler(0x8002));
    EXPECT_TRUE(sd->hasEventGroup(0x8002));
}

TEST_F(SomeipDaemonTest, ContractBEventGroupMissingNoEventOffer) {
    ServiceConfig config = contractBConfig();
    config.method_exchange_vehicle_message_id = 0x0002;
    config.event_vehicle_message_id = 0x8002;
    // eventgroup_id 未分配 → Event 整链不注册/不 Offer（fail-closed，不回退默认值）
    ASSERT_TRUE(daemon->initialize(config));
    ASSERT_TRUE(daemon->start());
    auto* sd = daemon->serviceDiscovery();
    ASSERT_NE(sd, nullptr);
    EXPECT_TRUE(sd->hasMethodHandler(0x0002));
    // Eventgroup 缺失 → Event handler 与 Eventgroup 均不注册（零 Offer Event）
    EXPECT_FALSE(sd->hasEventHandler(0x8002));
    EXPECT_FALSE(sd->hasEventGroup(0x8002));
}

TEST_F(SomeipDaemonTest, ReservedLegacyIdsZeroRegistration) {
    // 即使配置误填 reserved 旧 ID（0x0001/0x8001），也绝不注册（fail-closed，
    // 无 legacy fallback，US-017 不得复用）
    ServiceConfig config = contractBConfig();
    config.method_exchange_vehicle_message_id = reserved_ids::REPORT_SOFTWARE_INVENTORY;  // 0x0001
    config.event_vehicle_message_id = reserved_ids::FOTA_COMMAND;                         // 0x8001
    config.eventgroup_id = 7;
    ASSERT_TRUE(daemon->initialize(config));
    ASSERT_TRUE(daemon->start());
    auto* sd = daemon->serviceDiscovery();
    ASSERT_NE(sd, nullptr);
    EXPECT_FALSE(sd->hasMethodHandler(reserved_ids::REPORT_SOFTWARE_INVENTORY));
    EXPECT_FALSE(sd->hasEventHandler(reserved_ids::FOTA_COMMAND));
    EXPECT_FALSE(sd->hasEventGroup(reserved_ids::FOTA_COMMAND));
}

TEST_F(SomeipDaemonTest, ShutdownWithContractBRegistered) {
    // ID 完整 + 注册后启停：shutdown 顺序（StopOffer→拒绝→取消订阅→清理→停止
    // runtime）不应崩溃/泄漏
    ServiceConfig config = contractBConfig();
    config.method_exchange_vehicle_message_id = 0x0002;
    config.event_vehicle_message_id = 0x8002;
    config.eventgroup_id = 7;
    ASSERT_TRUE(daemon->initialize(config));
    ASSERT_TRUE(daemon->start());
    EXPECT_TRUE(daemon->isRunning());
    daemon->shutdown();
    EXPECT_FALSE(daemon->isRunning());
}
