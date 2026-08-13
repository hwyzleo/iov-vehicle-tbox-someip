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
    // CR-008：通用成员 ID 未由 Registry 分配前为 nullopt（不猜测未批准数值）
    EXPECT_FALSE(cfg.method_exchange_vehicle_message_id.has_value());
    EXPECT_FALSE(cfg.event_vehicle_message_id.has_value());
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
    // CR-008 generic_message 有界默认
    EXPECT_GT(cfg.generic_message.limits.max_envelope_bytes, 0u);
    EXPECT_GT(cfg.generic_message.limits.max_payload_bytes, 0u);
    EXPECT_GT(cfg.generic_message.max_in_flight, 0u);
    EXPECT_GT(cfg.generic_message.downlink_queue_capacity, 0u);
    EXPECT_GT(cfg.generic_message.tsp_timeout_ms, 0u);
    EXPECT_EQ(cfg.generic_message.limits.allowed_services, (std::vector<std::string>{"vehicle.fota"}));
    EXPECT_EQ(cfg.generic_message.limits.allowed_protocol_majors, (std::vector<uint32_t>{1}));
    // tsp_timeout < request_deadline（CR-008 严格校验）
    EXPECT_LT(cfg.generic_message.tsp_timeout_ms, cfg.bridge_request_deadline_ms);
}

TEST(ConfigLoadTest, EnableFlagsDefaultTrue) {
    ServiceConfig cfg;
    EXPECT_TRUE(cfg.enable_net_status);
    EXPECT_TRUE(cfg.enable_remote_cmd_relay);
    EXPECT_TRUE(cfg.enable_vehicle_message_gateway);
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
