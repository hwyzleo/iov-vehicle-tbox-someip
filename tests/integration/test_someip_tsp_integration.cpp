// SOMEIP → TSP 集成测试（真实链路，非桩）
//
// 真实组件：TspClientAdapter → tbox::tsp::TspClient（真实 SDK）→
// framework-ipc（真实传输）→ mock TSP IPC peer（framework-ipc Server +
// TSP 通用方法处理，业务对端脚本化）。
// 业务层：VehicleMessageGateway + GenericMessageBridge（Envelope 校验/关联）。
// 覆盖：能力探测（非 DEGRADED）、真实上行 exchange（Accepted + RESPONSE
// Envelope 关联校验）、TSP Unavailable 映射、真实下行订阅 + EVENT 推送 →
// bridge worker → gateway handler。

#include <gtest/gtest.h>
#include "bridge/tsp_client_adapter.h"
#include "ipc_bridge.h"
#include "services/vehicle_message_gateway.h"
#include "mock_tsp_peer.h"
#include "vehicle/common/v1/vehicle_message_envelope.pb.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace tbox::someip;
using tbox::someip::test::MockTspPeer;
using tbox::someip::test::unique_test_socket;

namespace {

// 构造合法 Envelope frame（CR-008：允许的 service/payloadType/TTL/messageId）
VehicleMessageFrame makeEnvelopeFrame(const std::string& message_id,
                                      const std::string& correlation_id,
                                      const std::string& message_kind) {
    vehicle::common::v1::VehicleMessageEnvelope env;
    env.set_request_id("req-0001");
    env.set_timestamp_ms(1000);
    env.set_protocol_version("fota-v1");
    env.set_service("vehicle.fota");
    env.set_payload_type("vehicle.fota.v1.CheckTask");
    env.set_payload("{}");
    env.set_message_id(message_id);
    if (!correlation_id.empty()) env.set_correlation_id(correlation_id);
    if (message_kind == "RESPONSE") {
        env.set_message_kind(vehicle::common::v1::MESSAGE_KIND_RESPONSE);
    } else if (message_kind == "EVENT") {
        env.set_message_kind(vehicle::common::v1::MESSAGE_KIND_EVENT);
    } else {
        env.set_message_kind(vehicle::common::v1::MESSAGE_KIND_REQUEST);
    }
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    env.set_expire_at_ms(now + 60000);

    std::string raw;
    env.SerializeToString(&raw);
    VehicleMessageFrame f;
    f.envelope_bytes.resize(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) {
        f.envelope_bytes[i] = static_cast<std::byte>(raw[i]);
    }
    return f;
}

GenericMessageConfig makeCfg() {
    GenericMessageConfig c;
    c.limits.allowed_services = {"vehicle.fota"};
    c.limits.allowed_protocol_versions = {"fota-v1"};
    c.limits.max_envelope_bytes = 16384;
    c.limits.max_payload_bytes = 8192;
    c.max_in_flight = 4;
    c.downlink_queue_capacity = 4;
    c.request_deadline_ms = 3000;
    c.tsp_timeout_ms = 2500;
    return c;
}

} // namespace

class SomeipTspIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        socket_path_ = unique_test_socket("tsp-int");
        peer_ = std::make_unique<MockTspPeer>(socket_path_);
        ASSERT_TRUE(peer_->start());

        TspAdapterConfig tsp_cfg;
        tsp_cfg.socket_path = socket_path_;
        tsp_cfg.exchange_timeout_ms = 1000;
        tsp_cfg.max_response_bytes = 16384;
        adapter_ = std::make_shared<TspClientAdapter>(tsp_cfg);
        ASSERT_TRUE(adapter_->initialize());

        bridge_ = std::make_unique<IpcBridge>();
        ASSERT_TRUE(bridge_->initialize());
        ASSERT_TRUE(bridge_->registerClient("tsp", adapter_));

        gateway_ = std::make_unique<VehicleMessageGateway>(
            bridge_.get(), 0x6101, 0x0001, makeCfg());
        ASSERT_TRUE(gateway_->initialize());
        ASSERT_TRUE(gateway_->start());
        // 真实能力就绪：桥接非 DEGRADED（不再是永久桩）
        EXPECT_FALSE(gateway_->isDegraded());
    }

    void TearDown() override {
        if (gateway_) gateway_->stop();
        if (bridge_) bridge_->shutdown();
        if (peer_) peer_->stop();
        std::remove(socket_path_.c_str());
    }

    std::string socket_path_;
    std::unique_ptr<MockTspPeer> peer_;
    std::shared_ptr<TspClientAdapter> adapter_;
    std::unique_ptr<IpcBridge> bridge_;
    std::unique_ptr<VehicleMessageGateway> gateway_;
};

// 真实上行：Accepted + RESPONSE Envelope（bridge 关联校验通过）
TEST_F(SomeipTspIntegrationTest, RealExchangeAcceptedRoundTrip) {
    auto request = makeEnvelopeFrame("msg-001", "", "REQUEST");
    peer_->set_exchange_response(
        "Accepted", 0, "",
        makeEnvelopeFrame("resp-1", "msg-001", "RESPONSE").envelope_bytes);

    auto resp = gateway_->exchangeVehicleMessage(request);
    EXPECT_EQ(resp.outcome, TransportOutcome::Accepted);
    ASSERT_TRUE(resp.response.has_value());
    EXPECT_FALSE(resp.response->envelope_bytes.empty());
    EXPECT_EQ(peer_->exchange_calls(), 1);
}

// 真实上行：correlation 错配 → ProtocolError（US-014）
TEST_F(SomeipTspIntegrationTest, CorrelationMismatchIsProtocolError) {
    auto request = makeEnvelopeFrame("msg-001", "", "REQUEST");
    peer_->set_exchange_response(
        "Accepted", 0, "",
        makeEnvelopeFrame("resp-1", "msg-WRONG", "RESPONSE").envelope_bytes);

    auto resp = gateway_->exchangeVehicleMessage(request);
    EXPECT_EQ(resp.outcome, TransportOutcome::ProtocolError);
}

// 真实上行：TSP 能力/route 不可用 → Unavailable（明确失败，不回退）
TEST_F(SomeipTspIntegrationTest, TspUnavailableMapsToUnavailable) {
    auto request = makeEnvelopeFrame("msg-001", "", "REQUEST");
    peer_->set_exchange_response("Unavailable", 1001, "route_unavailable", {});

    auto resp = gateway_->exchangeVehicleMessage(request);
    EXPECT_EQ(resp.outcome, TransportOutcome::Unavailable);
    ASSERT_TRUE(resp.error.has_value());
    EXPECT_FALSE(resp.error->summary.empty());
}

// 真实下行：mock TSP peer 推送 EVENT → bridge 校验入队 → worker → gateway handler
TEST_F(SomeipTspIntegrationTest, RealDownlinkEvent) {
    std::atomic<bool> received{false};
    VehicleMessageFrame got;
    gateway_->setDownlinkHandler([&](const VehicleMessageFrame& frame) {
        got = frame;
        received.store(true);
    });

    auto downlink = makeEnvelopeFrame("dl-1", "msg-001", "EVENT");
    bool pushed = false;
    for (int i = 0; i < 100 && !pushed; ++i) {
        pushed = peer_->push_downlink("vehicle.fota", downlink.envelope_bytes);
        if (!pushed) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    EXPECT_TRUE(pushed) << "downlink push should reach a subscribed client";

    for (int i = 0; i < 100 && !received.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    EXPECT_TRUE(received.load());
    EXPECT_EQ(got.envelope_bytes, downlink.envelope_bytes);
}
