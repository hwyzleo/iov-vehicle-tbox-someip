// TspClientAdapter 单元测试 (CR-005)
//
// 测试静态纯函数（DTO 转换、错误映射），
// 不依赖真实 TSP daemon socket。

#include <gtest/gtest.h>
#include "bridge/tsp_client_adapter.h"

using namespace tbox::someip;
using namespace tbox::tsp;

// ---- makeFotaMsgId ----

TEST(TspClientAdapterTest, MakeFotaMsgIdStable) {
    // 同一 snapshot_seq 生成相同 msg_id，保证 unknown 时单次重试复用
    EXPECT_EQ(TspClientAdapter::makeFotaMsgId(42), "someip-fota-42");
    EXPECT_EQ(TspClientAdapter::makeFotaMsgId(42), "someip-fota-42");
    EXPECT_EQ(TspClientAdapter::makeFotaMsgId(0), "someip-fota-0");
    EXPECT_NE(TspClientAdapter::makeFotaMsgId(1), TspClientAdapter::makeFotaMsgId(2));
}

// ---- toFotaSnapshot ----

TEST(TspClientAdapterTest, ToFotaSnapshotBasic) {
    VehicleSoftwareSnapshot snap;
    snap.vin = "TESTVIN1234567890";
    snap.snapshot_seq = 7;
    snap.registry_version = "1.0";

    EcuVersionEntry ecu1;
    ecu1.ecu_id = "ECU_A";
    ecu1.sw_version = "1.0.0";
    snap.ecu_list.push_back(ecu1);

    EcuVersionEntry ecu2;
    ecu2.ecu_id = "ECU_B";
    ecu2.sw_version = "2.1.3";
    snap.ecu_list.push_back(ecu2);

    auto fs = TspClientAdapter::toFotaSnapshot(snap, "msg-1", "trace-1", "req-1");

    EXPECT_EQ(fs.snapshot_seq, 7u);
    EXPECT_EQ(fs.msg_id, "msg-1");
    EXPECT_EQ(fs.trace_id, "trace-1");
    EXPECT_EQ(fs.request_id, "req-1");
    EXPECT_FALSE(fs.payload.empty());

    // payload 为纯文本 | 分隔格式，与 parseSoftwareInventoryPayload 对称
    std::string payload(fs.payload.begin(), fs.payload.end());
    EXPECT_NE(payload.find("TESTVIN1234567890"), std::string::npos);
    EXPECT_NE(payload.find("|7|2|"), std::string::npos);
    EXPECT_NE(payload.find("ECU_A,1.0.0"), std::string::npos);
    EXPECT_NE(payload.find("ECU_B,2.1.3"), std::string::npos);
}

TEST(TspClientAdapterTest, ToFotaSnapshotUnknownVersion) {
    VehicleSoftwareSnapshot snap;
    snap.vin = "VIN";
    snap.snapshot_seq = 1;

    EcuVersionEntry ecu;
    ecu.ecu_id = "ECU_X";
    ecu.sw_version = std::nullopt;  // unknown
    snap.ecu_list.push_back(ecu);

    auto fs = TspClientAdapter::toFotaSnapshot(snap, "m", "", "");
    std::string payload(fs.payload.begin(), fs.payload.end());
    EXPECT_NE(payload.find("ECU_X,unknown"), std::string::npos);
}

// ---- toBridgeResult ----

TEST(TspClientAdapterTest, ToBridgeResultSuccess) {
    ReportResult r;
    r.accepted = true;
    r.outcome = PublishOutcome::ACCEPTED;
    r.error_code = 0;
    r.msg_id = "msg-1";

    auto br = TspClientAdapter::toBridgeResult(r);
    EXPECT_EQ(br.outcome, BridgeOutcome::SUCCESS);
    EXPECT_EQ(br.error_code, 0);
    EXPECT_EQ(br.msg_id, "msg-1");
}

TEST(TspClientAdapterTest, ToBridgeResultUnknown) {
    // 请求已发送但响应丢失：不伪成功
    ReportResult r;
    r.accepted = false;
    r.outcome = PublishOutcome::UNKNOWN;
    r.error_code = static_cast<int32_t>(TspErrorCode::UNKNOWN_OUTCOME);
    r.msg_id = "msg-1";

    auto br = TspClientAdapter::toBridgeResult(r);
    EXPECT_EQ(br.outcome, BridgeOutcome::UNKNOWN);
    EXPECT_NE(br.outcome, BridgeOutcome::SUCCESS);
    EXPECT_EQ(br.error_code, static_cast<int32_t>(TspErrorCode::UNKNOWN_OUTCOME));
    EXPECT_EQ(br.msg_id, "msg-1");
}

TEST(TspClientAdapterTest, ToBridgeResultBusinessFailed) {
    ReportResult r;
    r.accepted = false;
    r.outcome = PublishOutcome::ACCEPTED;
    r.error_code = static_cast<int32_t>(TspErrorCode::PUBLISH_FAILED);

    auto br = TspClientAdapter::toBridgeResult(r);
    EXPECT_EQ(br.outcome, BridgeOutcome::FAILED);
}

TEST(TspClientAdapterTest, ToBridgeResultAcceptedButUnknownOutcome) {
    // 边界：accepted=true 但 outcome=UNKNOWN -> 仍为 UNKNOWN（不伪成功）
    ReportResult r;
    r.accepted = true;
    r.outcome = PublishOutcome::UNKNOWN;

    auto br = TspClientAdapter::toBridgeResult(r);
    EXPECT_EQ(br.outcome, BridgeOutcome::UNKNOWN);
}

// ---- toSomeipFotaCommand ----

TEST(TspClientAdapterTest, ToSomeipFotaCommandPreservesFields) {
    tbox::tsp::FotaCommand cmd;
    cmd.command_id = "cmd-1";
    cmd.delivery_id = "del-1";
    cmd.schema_version = "1.0";
    cmd.trace_id = "trace-1";
    cmd.request_id = "req-1";
    cmd.payload = {0x01, 0x02, 0x03};

    auto sc = TspClientAdapter::toSomeipFotaCommand(cmd);

    // CR-005 §5: 保持 command_id/delivery_id/trace_id/request_id
    EXPECT_EQ(sc.command_id, "cmd-1");
    EXPECT_EQ(sc.delivery_id, "del-1");
    EXPECT_EQ(sc.trace_id, "trace-1");
    EXPECT_EQ(sc.request_id, "req-1");
    EXPECT_EQ(sc.payload.size(), 3u);
    EXPECT_EQ(static_cast<uint8_t>(sc.payload[0]), 0x01);
}

TEST(TspClientAdapterTest, ToSomeipFotaCommandEmptyPayload) {
    tbox::tsp::FotaCommand cmd;
    cmd.command_id = "cmd-2";
    cmd.payload = {};

    auto sc = TspClientAdapter::toSomeipFotaCommand(cmd);
    EXPECT_EQ(sc.command_id, "cmd-2");
    EXPECT_TRUE(sc.payload.empty());
}
