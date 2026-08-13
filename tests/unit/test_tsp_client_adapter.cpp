// TspClientAdapter 单元测试（CR-005 / CR-008）
//
// CR-008：旧 FOTA 专用 DTO/codec 已删除；通用 vehicle message 依赖后续
// 版本化 tbox::tsp_client 通用消息 API（配对 CR），当前真实 facade 能力未就绪，
// supportsVehicleMessage()=false → 桥接 DEGRADED（US-013）。
// 本测试不依赖真实 TSP daemon socket。

#include <gtest/gtest.h>
#include "bridge/tsp_client_adapter.h"

using namespace tbox::someip;

// ---- 通用能力探测（CR-008 US-013）----

TEST(TspClientAdapterTest, GenericCapabilityNotReady) {
    // 真实 tbox::tsp::TspClient facade 尚无通用 vehicle message API（配对 CR）
    TspClientAdapter adapter;
    EXPECT_FALSE(adapter.supportsVehicleMessage());
}

TEST(TspClientAdapterTest, ExchangeReturnsUnavailableWhenCapabilityNotReady) {
    TspClientAdapter adapter;
    VehicleMessageFrame frame;
    frame.envelope_bytes = {std::byte{0x0a}, std::byte{0x00}};

    auto resp = adapter.exchangeVehicleMessage(frame);
    EXPECT_EQ(resp.outcome, TransportOutcome::Unavailable);
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_EQ(resp.error->code, static_cast<int32_t>(ErrorCode::CLIENT_CALL_FAILED));
}

TEST(TspClientAdapterTest, SubscribeVehicleMessageNoop) {
    // DEGRADED 语义：能力未就绪不建立下行订阅，回调不触发，不崩溃
    TspClientAdapter adapter;
    bool called = false;
    adapter.subscribeVehicleMessage("vehicle.fota",
        [&called](const VehicleMessageFrame&) { called = true; });
    EXPECT_FALSE(called);
}

TEST(TspClientAdapterTest, LifecycleNoCrash) {
    // 后端 daemon 未就绪时 initialize() 返回 false（按需重连），shutdown 幂等
    TspClientAdapter adapter;
    adapter.initialize();
    adapter.shutdown();
    adapter.shutdown();
}

// ---- 未迁移能力占位（非 CR-008 范围，保持不崩溃）----

TEST(TspClientAdapterTest, UnmigratedCapabilitiesNoop) {
    TspClientAdapter adapter;
    NetStatusData st;
    EXPECT_EQ(adapter.getNetStatus(st), ErrorCode::SUCCESS);
    adapter.subscribeNetStatusChanges([](const NetStatusData&) {});
    adapter.subscribeRemoteCommands([](const RemoteCommand&) {});
}
