#include "fota_relay.h"
#include "ipc_bridge.h"
#include "tbox_someip/someip_log_adapter.h"
#include "tbox_someip/someip_context.h"

#include <chrono>

namespace tbox {
namespace someip {

FotaRelayService::FotaRelayService(IpcBridge* bridge)
    : bridge_(bridge)
    , state_(ServiceState::UNAVAILABLE)
    , running_(false) {}

FotaRelayService::~FotaRelayService() {
    stop();
}

bool FotaRelayService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::UNAVAILABLE) {
        SomeipLogAdapter::fota_relay().warn("someip.fota.already_initialized",
            "FotaRelayService already initialized"
        );
        return false;
    }

    if (!bridge_ || !bridge_->isInitialized()) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "IpcBridge not initialized",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("initialize")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return false;
    }

    auto client = bridge_->getClient("tsp");
    if (!client) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "TSP client not found",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("get_client")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return false;
    }

    tsp_client_ = std::dynamic_pointer_cast<ITspClient>(client);
    if (!tsp_client_) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "Invalid TSP client type",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("cast")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return false;
    }

    if (!tsp_client_->initialize()) {
            // 非致命：后端 daemon 未就绪时按需重连（framework-ipc client 含重连重试），
            // 不阻塞门面服务启动。
            SomeipLogAdapter::ipc_bridge().warn("someip.ipc.tsp.connect_deferred",
                "TSP client not connected yet; will reconnect on demand",
                {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
                 tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("initialize")),
                 tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("deferred"))}
            );
        }

    state_ = ServiceState::INITIALIZING;
    SomeipLogAdapter::fota_relay().info("someip.fota.initialized",
        "FotaRelayService initialized"
    );
    return true;
}

bool FotaRelayService::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::INITIALIZING) {
        SomeipLogAdapter::fota_relay().warn("someip.fota.start_failed",
            "FotaRelayService not in initializing state"
        );
        return false;
    }

    running_ = true;
    worker_thread_ = std::thread(&FotaRelayService::run, this);

    state_ = ServiceState::AVAILABLE;
    SomeipLogAdapter::fota_relay().info("someip.fota.started",
        "FotaRelayService started"
    );
    return true;
}

void FotaRelayService::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);

        if (state_ == ServiceState::UNAVAILABLE) {
            return;
        }

        running_ = false;
    }

    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);

    if (tsp_client_) {
        tsp_client_->shutdown();
        tsp_client_.reset();
    }

    fota_callbacks_.clear();
    state_ = ServiceState::UNAVAILABLE;

    SomeipLogAdapter::fota_relay().info("someip.fota.stopped",
        "FotaRelayService stopped"
    );
}

ServiceState FotaRelayService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

uint16_t FotaRelayService::getServiceId() const {
    return someip_addressing::SERVICE_ID;
}

uint16_t FotaRelayService::getInstanceId() const {
    return someip_addressing::INSTANCE_ID;
}

BridgeCallResult FotaRelayService::reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::AVAILABLE) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "FotaRelayService not available",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("report_software_inventory")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return BridgeCallResult{BridgeOutcome::FAILED,
            static_cast<int32_t>(ErrorCode::CLIENT_CALL_FAILED), ""};
    }

    if (!tsp_client_) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "TSP client not available",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("report_software_inventory")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return BridgeCallResult{BridgeOutcome::FAILED,
            static_cast<int32_t>(ErrorCode::CLIENT_CALL_FAILED), ""};
    }

    auto start_time = std::chrono::steady_clock::now();
    BridgeCallResult result = tsp_client_->reportSoftwareInventory(snapshot);
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();

    std::string result_str;
    std::string error_code_str;
    switch (result.outcome) {
        case BridgeOutcome::SUCCESS:
            result_str = "succeeded";
            break;
        case BridgeOutcome::UNKNOWN:
            // 请求已发送但响应丢失：不返回伪成功，明确标注不确定结果
            result_str = "unknown";
            error_code_str = "TBOX-SOMEIP-1003";
            break;
        case BridgeOutcome::FAILED:
        default:
            result_str = "failed";
            error_code_str = "TBOX-SOMEIP-1003";
            break;
    }

    // FOTA 上行：VIN 按 Identifier 脱敏，payload 仅记录长度/条目数
    if (result.outcome == BridgeOutcome::SUCCESS) {
        SomeipLogAdapter::fota_relay().info("someip.fota.inventory.relayed",
            "Software inventory relayed to TSP",
            {tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("up")),
             tbox::fw::log::Field("entry_count", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(snapshot.ecu_list.size()))),
             tbox::fw::log::Field("snapshot_seq", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(snapshot.snapshot_seq))),
             tbox::fw::log::Field("vin", tbox::fw::log::FieldValue::makeString(snapshot.vin),
                 tbox::fw::log::Sensitivity::Identifier),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString(result_str)),
             tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(duration))}
        );
    } else {
        SomeipLogAdapter::fota_relay().error("someip.fota.inventory.relayed",
            "Failed to relay software inventory to TSP",
            {tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("up")),
             tbox::fw::log::Field("entry_count", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(snapshot.ecu_list.size()))),
             tbox::fw::log::Field("snapshot_seq", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(snapshot.snapshot_seq))),
             tbox::fw::log::Field("vin", tbox::fw::log::FieldValue::makeString(snapshot.vin),
                 tbox::fw::log::Sensitivity::Identifier),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString(result_str)),
             tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(duration)),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString(error_code_str))}
        );
    }

    return result;
}

void FotaRelayService::subscribeFotaCommands(FotaCommandCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    fota_callbacks_.push_back(callback);

    // 预留：注册到 tsp_client
    if (tsp_client_) {
        tsp_client_->subscribeFotaCommands([this](const FotaCommand& command) {
            notifyFotaCommand(command);
        });
    }
}

void FotaRelayService::notifyFotaCommand(const FotaCommand& command) {
    std::lock_guard<std::mutex> lock(mutex_);

    // FOTA 下行：命令 payload 仅记录长度/摘要，不输出完整内容
    SomeipLogAdapter::fota_relay().info("someip.fota.command.relayed",
        "FOTA command relayed to CGW",
        {tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("down")),
         tbox::fw::log::Field("command_id", tbox::fw::log::FieldValue::makeString(command.command_id)),
         tbox::fw::log::Field("command_type", tbox::fw::log::FieldValue::makeString(command.command_type)),
         tbox::fw::log::Field("payload_length", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(command.payload.size())),
             tbox::fw::log::Sensitivity::Payload),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("succeeded"))}
    );

    for (auto& callback : fota_callbacks_) {
        if (callback) {
            callback(command);
        }
    }
}

void FotaRelayService::run() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace someip
} // namespace tbox
