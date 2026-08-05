// TspClientAdapter 实现 (CR-005)
//
// 封装 tbox::tsp::TspClient facade。

#include "tsp_client_adapter.h"

#include <chrono>
#include <sstream>

namespace tbox {
namespace someip {

namespace {

// 序列化 VehicleSoftwareSnapshot 为纯文本 | 分隔格式
// （与 someip_daemon.cpp::parseSoftwareInventoryPayload 对称）
std::vector<uint8_t> serializeSnapshot(const VehicleSoftwareSnapshot& snap) {
    std::string text = snap.vin + "|" +
        std::to_string(snap.snapshot_seq) + "|" +
        std::to_string(snap.ecu_list.size());
    for (const auto& ecu : snap.ecu_list) {
        text += "|" + ecu.ecu_id + "," + ecu.sw_version.value_or("unknown");
    }
    return std::vector<uint8_t>(text.begin(), text.end());
}

} // anonymous namespace

// ============================================================
// 静态纯函数（可单测）
// ============================================================

std::string TspClientAdapter::makeFotaMsgId(uint64_t snapshot_seq) {
    // 基于 snapshot_seq 生成稳定 msg_id，保证 unknown outcome 时单次重试复用
    return "someip-fota-" + std::to_string(snapshot_seq);
}

tbox::tsp::FotaSnapshot TspClientAdapter::toFotaSnapshot(
    const VehicleSoftwareSnapshot& snap,
    const std::string& msg_id,
    const std::string& trace_id,
    const std::string& request_id) {
    tbox::tsp::FotaSnapshot fs;
    fs.snapshot_seq = static_cast<uint32_t>(snap.snapshot_seq);
    fs.msg_id = msg_id;
    fs.content_type = "text/plain";
    fs.payload = serializeSnapshot(snap);
    fs.trace_id = trace_id;
    fs.request_id = request_id;
    return fs;
}

BridgeCallResult TspClientAdapter::toBridgeResult(const tbox::tsp::ReportResult& r) {
    BridgeCallResult br;
    br.msg_id = r.msg_id;
    br.error_code = r.error_code;
    if (r.accepted && r.outcome == tbox::tsp::PublishOutcome::ACCEPTED) {
        br.outcome = BridgeOutcome::SUCCESS;
    } else if (r.outcome == tbox::tsp::PublishOutcome::UNKNOWN) {
        // 请求已发送但响应丢失：不伪成功，明确不确定结果 (CR-005 §4.2)
        br.outcome = BridgeOutcome::UNKNOWN;
    } else {
        br.outcome = BridgeOutcome::FAILED;
    }
    return br;
}

FotaCommand TspClientAdapter::toSomeipFotaCommand(const tbox::tsp::FotaCommand& cmd) {
    // 保持 command_id/delivery_id/trace_id/request_id (CR-005 §5)
    FotaCommand sc;
    sc.command_id = cmd.command_id;
    sc.delivery_id = cmd.delivery_id;
    sc.command_type = cmd.schema_version;
    sc.payload.assign(reinterpret_cast<const char*>(cmd.payload.data()), cmd.payload.size());
    sc.trace_id = cmd.trace_id;
    sc.request_id = cmd.request_id;
    return sc;
}

// ============================================================
// 构造/析构
// ============================================================

TspClientAdapter::TspClientAdapter(const TspAdapterConfig& config)
    : config_(config)
    , tsp_client_(std::make_unique<tbox::tsp::TspClient>(config.socket_path)) {
}

TspClientAdapter::~TspClientAdapter() {
    shutdown();
}

// ============================================================
// IIpcClient
// ============================================================

bool TspClientAdapter::initialize() {
    if (!tsp_client_) {
        return false;
    }
    bool ok = tsp_client_->connect();
    SomeipLogAdapter::ipc_bridge().info("someip.ipc.tsp.connect",
        "TSP client adapter initialize",
        {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
         tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("initialize")),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString(ok ? "succeeded" : "failed"))}
    );
    return ok;
}

bool TspClientAdapter::isConnected() const {
    return tsp_client_ && tsp_client_->is_connected();
}

void TspClientAdapter::shutdown() {
    // 顺序 (CR-005 §7)：先取消订阅 -> 停 worker -> 断开 client
    subscription_.cancel();
    stopEventWorker();

    if (tsp_client_) {
        tsp_client_->disconnect();
    }
    SomeipLogAdapter::ipc_bridge().info("someip.ipc.tsp.shutdown",
        "TSP client adapter shutdown"
    );
}

// ============================================================
// FOTA 上行
// ============================================================

BridgeCallResult TspClientAdapter::reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot) {
    if (!tsp_client_ || !tsp_client_->is_connected()) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "TSP client not connected",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("report_software_inventory")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return BridgeCallResult{BridgeOutcome::FAILED,
            static_cast<int32_t>(ErrorCode::CLIENT_CALL_FAILED), ""};
    }

    // 复用 snapshot_seq 生成稳定 msg_id，保证 unknown 时单次重试复用 (CR-005 §4.2)
    std::string msg_id = makeFotaMsgId(snapshot.snapshot_seq);
    tbox::tsp::FotaSnapshot fs = toFotaSnapshot(snapshot, msg_id, "", msg_id);

    auto start_time = std::chrono::steady_clock::now();
    tbox::tsp::ReportResult r = tsp_client_->reportSoftwareInventory(fs);
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();

    BridgeCallResult result = toBridgeResult(r);

    std::string result_str;
    switch (result.outcome) {
        case BridgeOutcome::SUCCESS: result_str = "succeeded"; break;
        case BridgeOutcome::UNKNOWN: result_str = "unknown";   break;
        default:                     result_str = "failed";    break;
    }

    // 不记录原始 payload / VIN 明文 (CR-005 §8)
    SomeipLogAdapter::ipc_bridge().info("someip.ipc.tsp.report",
        "FOTA inventory report via tsp_client",
        {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
         tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("report_software_inventory")),
         tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("up")),
         tbox::fw::log::Field("snapshot_seq", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(snapshot.snapshot_seq))),
         tbox::fw::log::Field("entry_count", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(snapshot.ecu_list.size()))),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString(result_str)),
         tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(duration))}
    );

    return result;
}

// ============================================================
// FOTA 下行
// ============================================================

void TspClientAdapter::subscribeFotaCommands(FotaCommandCallback callback) {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        fota_callback_ = callback;
    }

    // 启动 event worker：独立线程完成回调，不阻塞 framework subscription 读线程 (CR-005 §5)
    if (!worker_running_) {
        worker_running_ = true;
        worker_thread_ = std::thread(&TspClientAdapter::eventWorkerLoop, this);
    }

    // 建立独立订阅连接，完成 Response 后进入 event-only (CR-005 §5)
    if (tsp_client_) {
        subscription_ = tsp_client_->subscribeFotaCommand(
            [this](const tbox::tsp::FotaCommand& cmd) {
                // subscription callback 只做转换和投递，不阻塞
                FotaCommand someip_cmd = toSomeipFotaCommand(cmd);
                {
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    if (event_queue_.size() >= config_.event_queue_size) {
                        // 背压：drop_oldest，保持单源顺序，慢 bridge 不阻塞 I/O (CR-005 §4.4)
                        event_queue_.pop();
                    }
                    event_queue_.push(std::move(someip_cmd));
                }
                queue_cv_.notify_one();
            });

        SomeipLogAdapter::ipc_bridge().info("someip.ipc.tsp.subscribe",
            "FOTA command subscription established",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("down")),
             tbox::fw::log::Field("active", tbox::fw::log::FieldValue::makeBool(subscription_.isActive())),
             tbox::fw::log::Field("queue_size", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(config_.event_queue_size)))}
        );
    }
}

// ============================================================
// 未迁移能力（占位，对应 facade 未就绪，属后续 CR）
// ============================================================

ErrorCode TspClientAdapter::getNetStatus(NetStatusData& status) {
    (void)status;
    return ErrorCode::SUCCESS;
}

void TspClientAdapter::subscribeNetStatusChanges(NetStatusCallback callback) {
    (void)callback;
}

void TspClientAdapter::subscribeRemoteCommands(RemoteCommandCallback callback) {
    (void)callback;
}

// ============================================================
// event worker
// ============================================================

void TspClientAdapter::eventWorkerLoop() {
    while (worker_running_) {
        FotaCommand cmd;
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] {
                return !event_queue_.empty() || !worker_running_;
            });
            if (!worker_running_ && event_queue_.empty()) {
                break;
            }
            if (event_queue_.empty()) {
                continue;
            }
            cmd = std::move(event_queue_.front());
            event_queue_.pop();
        }

        // 在 worker 线程完成回调，不阻塞 framework subscription 读线程
        FotaCommandCallback cb;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            cb = fota_callback_;
        }
        if (cb) {
            cb(cmd);
        }
    }
}

void TspClientAdapter::stopEventWorker() {
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        worker_running_ = false;
    }
    queue_cv_.notify_all();
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

} // namespace someip
} // namespace tbox
