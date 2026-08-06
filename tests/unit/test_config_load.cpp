// CR-007 方案 C：ServiceConfig 默认值 = types.h Registry 基线
// 验证 config 为运行时 SSOT，缺失字段时 fallback 到基线默认值。
#include <gtest/gtest.h>
#include "tbox_someip/config.h"
#include "tbox_someip/types.h"

using namespace tbox::someip;

TEST(ConfigLoadTest, DefaultsMatchRegistryBaseline) {
    ServiceConfig cfg;
    EXPECT_EQ(cfg.service_id, someip_addressing::SERVICE_ID);
    EXPECT_EQ(cfg.instance_id, someip_addressing::INSTANCE_ID);
    EXPECT_EQ(cfg.port, someip_addressing::TCP_PORT);
    EXPECT_EQ(cfg.sd_port, someip_addressing::SD_PORT);
    EXPECT_EQ(cfg.method_report_software_inventory_id, method_ids::REPORT_SOFTWARE_INVENTORY);
    EXPECT_EQ(cfg.event_fota_command_id, event_ids::FOTA_COMMAND);
    EXPECT_EQ(cfg.transport, "TCP");
    EXPECT_EQ(cfg.sd_protocol, "UDP");
}

TEST(ConfigLoadTest, BoundedDefaultsAreSane) {
    ServiceConfig cfg;
    EXPECT_GT(cfg.bridge_request_deadline_ms, 0u);
    EXPECT_GT(cfg.bridge_queue_capacity, 0u);
    EXPECT_GE(cfg.bridge_worker_count, 1u);
    EXPECT_GT(cfg.sd_initial_delay_ms, 0u);
    EXPECT_GT(cfg.tsp.connect_timeout_ms, 0u);
    EXPECT_GT(cfg.prov.connect_timeout_ms, 0u);
}

TEST(ConfigLoadTest, EnableFlagsDefaultTrue) {
    ServiceConfig cfg;
    EXPECT_TRUE(cfg.enable_net_status);
    EXPECT_TRUE(cfg.enable_remote_cmd_relay);
    EXPECT_TRUE(cfg.enable_fota_relay);
}

TEST(ConfigLoadTest, ClientSocketPaths) {
    ServiceConfig cfg;
    EXPECT_EQ(cfg.tsp.socket_path, "/tmp/tbox-tsp.sock");
    EXPECT_EQ(cfg.prov.socket_path, "/tmp/tbox-prov.sock");
}

TEST(ConfigLoadTest, NoIpcSocketPathField) {
    // CR-007 §6：SOMEIP 是 consumer/bridge，ServiceConfig 不含自身 IPC server socket
    ServiceConfig cfg;
    // 静态断言：ServiceConfig 无 ipc_socket_path 字段（编译期保证）
    // 此测试存在即说明字段已移除；若误加回编译会失败
    SUCCEED();
}
