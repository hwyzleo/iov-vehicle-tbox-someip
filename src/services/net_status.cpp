#include "net_status.h"
#include "ipc_bridge.h"
#include "tbox_someip/someip_log_adapter.h"

#include <chrono>

namespace tbox {
namespace someip {

NetStatusService::NetStatusService(IpcBridge* bridge)
    : bridge_(bridge)
    , state_(ServiceState::UNAVAILABLE)
    , running_(false) {}

NetStatusService::~NetStatusService() {
    stop();
}

bool NetStatusService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::UNAVAILABLE) {
        SomeipLogAdapter::method().warn("someip.net_status.already_initialized",
            "NetStatusService already initialized"
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

    // 获取 TSP 客户端
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

    // 初始化 TSP 客户端
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
    SomeipLogAdapter::method().info("someip.net_status.initialized",
        "NetStatusService initialized"
    );
    return true;
}

bool NetStatusService::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::INITIALIZING) {
        SomeipLogAdapter::method().warn("someip.net_status.start_failed",
            "NetStatusService not in initializing state"
        );
        return false;
    }

    // 启动工作线程
    running_ = true;
    worker_thread_ = std::thread(&NetStatusService::run, this);

    state_ = ServiceState::AVAILABLE;
    SomeipLogAdapter::method().info("someip.net_status.started",
        "NetStatusService started"
    );
    return true;
}

void NetStatusService::stop() {
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

    status_callbacks_.clear();
    state_ = ServiceState::UNAVAILABLE;

    SomeipLogAdapter::method().info("someip.net_status.stopped",
        "NetStatusService stopped"
    );
}

ServiceState NetStatusService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

uint16_t NetStatusService::getServiceId() const {
    return someip_addressing::SERVICE_ID;
}

uint16_t NetStatusService::getInstanceId() const {
    return someip_addressing::INSTANCE_ID;
}

ErrorCode NetStatusService::getNetStatus(NetStatusData& status) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::AVAILABLE) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "NetStatusService not available",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("get_net_status")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return ErrorCode::CLIENT_CALL_FAILED;
    }

    if (!tsp_client_ || !tsp_client_->isConnected()) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "TSP client not connected",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("get_net_status")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return ErrorCode::CLIENT_CALL_FAILED;
    }

    return tsp_client_->getNetStatus(status);
}

void NetStatusService::subscribeNetStatusChanges(NetStatusCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_callbacks_.push_back(callback);

    // 订阅网络状态变化
    if (tsp_client_) {
        tsp_client_->subscribeNetStatusChanges([this](const NetStatusData& status) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& cb : status_callbacks_) {
                if (cb) {
                    cb(status);
                }
            }
        });
    }
}

void NetStatusService::run() {
    while (running_) {
        // 定期检查网络状态
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

} // namespace someip
} // namespace tbox
