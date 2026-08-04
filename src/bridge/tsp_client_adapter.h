#pragma once

// TspClientAdapter (CR-005)
//
// 仅在 USE_REAL_IPC 模式下编译/引用。通过 tbox::tsp::TspClient
// (framework-ipc 域 facade) 访问 TSP daemon。
//
// 边界：
// - 不直接创建 framework Client、不解析 provider wire。
// - FOTA 上行: VehicleSoftwareSnapshot -> FotaSnapshot 转换，调 facade，
//   ReportResult -> BridgeCallResult 映射（含 UNKNOWN outcome，不伪成功）。
// - FOTA 下行: RAII Subscription + 有界队列背压，独立连接 event-only，
//   subscription callback 仅投递，转换/回调在独立 worker。
// - 未迁移能力（NetStatus/RemoteCmd）保持占位行为，对应 facade 未就绪。

#include "tbox_someip/interfaces.h"
#include "tbox_someip/types.h"
#include "tbox_someip/someip_log_adapter.h"

#include "tbox/tsp/client.h"   // TspClient, FotaSnapshot, ReportResult, FotaCommand
#include "tbox/tsp/errors.h"   // TspErrorCode
#include "ipc.h"               // tbox::fw::ipc::Subscription

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace tbox {
namespace someip {

/// TSP 适配器配置
struct TspAdapterConfig {
    std::string socket_path = "/tmp/tbox-tsp.sock";
    uint32_t default_timeout_ms = 3000;
    uint32_t event_queue_size = 256;
};

class TspClientAdapter : public ITspClient {
public:
    explicit TspClientAdapter(const TspAdapterConfig& config = TspAdapterConfig{});
    ~TspClientAdapter() override;

    TspClientAdapter(const TspClientAdapter&) = delete;
    TspClientAdapter& operator=(const TspClientAdapter&) = delete;

    // ---- IIpcClient ----
    bool initialize() override;
    bool isConnected() const override;
    void shutdown() override;

    // ---- FOTA 上行 ----
    BridgeCallResult reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot) override;

    // ---- FOTA 下行 ----
    void subscribeFotaCommands(FotaCommandCallback callback) override;

    // ---- 未迁移能力（占位，对应 facade 未就绪）----
    ErrorCode getNetStatus(NetStatusData& status) override;
    void subscribeNetStatusChanges(NetStatusCallback callback) override;
    void subscribeRemoteCommands(RemoteCommandCallback callback) override;

    // ---- 纯函数（可单测）----
    /// 生成 FOTA 上行幂等关联键 msg_id（基于 snapshot_seq，保证重试复用）
    static std::string makeFotaMsgId(uint64_t snapshot_seq);

    /// someip VehicleSoftwareSnapshot -> tsp FotaSnapshot
    static tbox::tsp::FotaSnapshot toFotaSnapshot(const VehicleSoftwareSnapshot& snap,
                                                   const std::string& msg_id,
                                                   const std::string& trace_id,
                                                   const std::string& request_id);

    /// tsp ReportResult -> BridgeCallResult（含 UNKNOWN outcome）
    static BridgeCallResult toBridgeResult(const tbox::tsp::ReportResult& r);

    /// tsp FotaCommand -> someip FotaCommand（保持 command_id/delivery_id/trace_id/request_id）
    static FotaCommand toSomeipFotaCommand(const tbox::tsp::FotaCommand& cmd);

private:
    void eventWorkerLoop();
    void stopEventWorker();

    TspAdapterConfig config_;
    std::unique_ptr<tbox::tsp::TspClient> tsp_client_;

    // FOTA 下行事件队列 + worker（背压：drop_oldest）
    std::queue<FotaCommand> event_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::atomic<bool> worker_running_{false};
    std::thread worker_thread_;
    FotaCommandCallback fota_callback_;

    // RAII 订阅句柄（析构自动取消）
    ::tbox::fw::ipc::Subscription subscription_;
};

} // namespace someip
} // namespace tbox
