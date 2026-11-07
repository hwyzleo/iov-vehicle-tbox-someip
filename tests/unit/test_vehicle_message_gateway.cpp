// VehicleMessageGateway 测试（CR-008）：生命周期 + 上行 exchange + 下行 handler。

#include <gtest/gtest.h>
#include "services/vehicle_message_gateway.h"
#include "ipc_bridge.h"

#include "vehicle/common/v1/vehicle_message_envelope.pb.h"

#include <chrono>
#include <thread>

using namespace tbox::someip;

namespace {

class FakeTspClient : public ITspClient {
public:
    bool initialize() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
    ErrorCode getNetStatus(NetStatusData&) override { return ErrorCode::SUCCESS; }
    void subscribeNetStatusChanges(NetStatusCallback) override {}
    void subscribeRemoteCommands(RemoteCommandCallback) override {}

    bool supports_ = true;
    bool supportsVehicleMessage() const override { return supports_; }

    VehicleMessageResponseFrame result_;
    VehicleMessageResponseFrame exchangeVehicleMessage(const VehicleMessageFrame&) override {
        return result_;
    }

    VehicleMessageHandler downlink_handler_;
    void subscribeVehicleMessage(std::string_view, VehicleMessageHandler handler) override {
        downlink_handler_ = std::move(handler);
    }
    bool isVehicleMessageDownlinkActive() const override { return true; }
    void pushDownlink(const VehicleMessageFrame& f) {
        if (downlink_handler_) downlink_handler_(f);
    }
};

VehicleMessageFrame makeValidFrame() {
    vehicle::common::v1::VehicleMessageEnvelope env;
    env.set_request_id("req-0001");
    env.set_timestamp_ms(1000);
    env.set_protocol_version("fota-v1");
    env.set_service("vehicle.fota");
    env.set_vin("VIN12345678901234");   // 云侧硬契约：Envelope.vin 必填且 17 位
    env.set_payload_type("vehicle.fota.v1.CheckTask");
    env.set_payload("{}");
    env.set_message_id("msg-001");
    env.set_message_kind(vehicle::common::v1::MESSAGE_KIND_REQUEST);
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    env.set_expire_at_ms(now + 60000);
    std::string raw;
    env.SerializeToString(&raw);
    VehicleMessageFrame f;
    f.envelope_bytes.resize(raw.size());
    for (size_t i = 0; i < raw.size(); ++i) f.envelope_bytes[i] = static_cast<std::byte>(raw[i]);
    return f;
}

class VehicleMessageGatewayTest : public ::testing::Test {
protected:
    void SetUp() override {
        bridge_ = std::make_unique<IpcBridge>();
        bridge_->initialize();
        fake_ = std::make_shared<FakeTspClient>();
        fake_->result_.outcome = TransportOutcome::Accepted;
        bridge_->registerClient("tsp", fake_);
        gateway_ = std::make_unique<VehicleMessageGateway>(
            bridge_.get(), someip_addressing::SERVICE_ID, someip_addressing::INSTANCE_ID,
            GenericMessageConfig{});
    }
    void TearDown() override {
        if (gateway_) gateway_->stop();
        if (bridge_) bridge_->shutdown();
    }

    std::unique_ptr<IpcBridge> bridge_;
    std::shared_ptr<FakeTspClient> fake_;
    std::unique_ptr<VehicleMessageGateway> gateway_;
};

} // namespace

TEST_F(VehicleMessageGatewayTest, InitializeStartLifecycle) {
    ASSERT_TRUE(gateway_->initialize());
    ASSERT_TRUE(gateway_->start());
    EXPECT_EQ(gateway_->getState(), ServiceState::AVAILABLE);
    EXPECT_FALSE(gateway_->isDegraded());
    EXPECT_EQ(gateway_->getServiceId(), someip_addressing::SERVICE_ID);
    EXPECT_EQ(gateway_->getInstanceId(), someip_addressing::INSTANCE_ID);
    gateway_->stop();
    EXPECT_EQ(gateway_->getState(), ServiceState::UNAVAILABLE);
}

TEST_F(VehicleMessageGatewayTest, InitializeWithoutBridge) {
    VehicleMessageGateway no_bridge(nullptr, 0x6101, 0x0001, GenericMessageConfig{});
    EXPECT_FALSE(no_bridge.initialize());
}

TEST_F(VehicleMessageGatewayTest, ExchangeAcceptedWhenAvailable) {
    ASSERT_TRUE(gateway_->initialize());
    ASSERT_TRUE(gateway_->start());
    auto r = gateway_->exchangeVehicleMessage(makeValidFrame());
    EXPECT_EQ(r.outcome, TransportOutcome::Accepted);
}

TEST_F(VehicleMessageGatewayTest, ExchangeUnavailableBeforeStart) {
    ASSERT_TRUE(gateway_->initialize());
    auto r = gateway_->exchangeVehicleMessage(makeValidFrame());
    EXPECT_EQ(r.outcome, TransportOutcome::Unavailable);
}

TEST_F(VehicleMessageGatewayTest, DegradedWhenTspCapabilityMissing) {
    fake_->supports_ = false;
    ASSERT_TRUE(gateway_->initialize());
    ASSERT_TRUE(gateway_->start());
    EXPECT_TRUE(gateway_->isDegraded());
    auto r = gateway_->exchangeVehicleMessage(makeValidFrame());
    EXPECT_EQ(r.outcome, TransportOutcome::Unavailable);
}

TEST_F(VehicleMessageGatewayTest, DownlinkHandlerWired) {
    ASSERT_TRUE(gateway_->initialize());
    ASSERT_TRUE(gateway_->start());

    std::atomic<int> received{0};
    gateway_->setDownlinkHandler([&received](const VehicleMessageFrame&) { ++received; });

    fake_->pushDownlink(makeValidFrame());
    fake_->pushDownlink(makeValidFrame());

    for (int i = 0; i < 50 && received.load() < 2; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    EXPECT_EQ(received.load(), 2);
}

TEST_F(VehicleMessageGatewayTest, StopIsIdempotent) {
    ASSERT_TRUE(gateway_->initialize());
    ASSERT_TRUE(gateway_->start());
    gateway_->stop();
    gateway_->stop();  // 幂等
    EXPECT_EQ(gateway_->getState(), ServiceState::UNAVAILABLE);
}
