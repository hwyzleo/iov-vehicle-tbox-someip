#include "remote_cmd_relay.h"
#include "ipc_bridge.h"
#include "tbox_someip/someip_log_adapter.h"

#include <chrono>

namespace tbox {
namespace someip {

RemoteCmdRelayService::RemoteCmdRelayService(IpcBridge* bridge, uint16_t service_id, uint16_t instance_id)
    : bridge_(bridge)
    , state_(ServiceState::UNAVAILABLE)
    , running_(false)
    , service_id_(service_id)
    , instance_id_(instance_id) {}

RemoteCmdRelayService::~RemoteCmdRelayService() {
    stop();
}

bool RemoteCmdRelayService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::UNAVAILABLE) {
        SomeipLogAdapter::method().warn("someip.remote_cmd.already_initialized",
            "RemoteCmdRelayService already initialized"
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
    SomeipLogAdapter::method().info("someip.remote_cmd.initialized",
        "RemoteCmdRelayService initialized"
    );
    return true;
}

bool RemoteCmdRelayService::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::INITIALIZING) {
        SomeipLogAdapter::method().warn("someip.remote_cmd.start_failed",
            "RemoteCmdRelayService not in initializing state"
        );
        return false;
    }

    // 订阅远程命令
    if (tsp_client_) {
        tsp_client_->subscribeRemoteCommands([this](const RemoteCommand& command) {
            notifyRemoteCommand(command);
        });
    }

    // 启动工作线程
    running_ = true;
    worker_thread_ = std::thread(&RemoteCmdRelayService::run, this);

    state_ = ServiceState::AVAILABLE;
    SomeipLogAdapter::method().info("someip.remote_cmd.started",
        "RemoteCmdRelayService started"
    );
    return true;
}

void RemoteCmdRelayService::stop() {
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

    command_callbacks_.clear();
    state_ = ServiceState::UNAVAILABLE;

    SomeipLogAdapter::method().info("someip.remote_cmd.stopped",
        "RemoteCmdRelayService stopped"
    );
}

ServiceState RemoteCmdRelayService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

uint16_t RemoteCmdRelayService::getServiceId() const {
    return service_id_;
}

uint16_t RemoteCmdRelayService::getInstanceId() const {
    return instance_id_;
}

void RemoteCmdRelayService::subscribeRemoteCommands(RemoteCommandCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    command_callbacks_.push_back(callback);
}

void RemoteCmdRelayService::notifyRemoteCommand(const RemoteCommand& command) {
    std::lock_guard<std::mutex> lock(mutex_);

    // IPC 通知转为 Event 下发，记录事件（payload 仅记录长度）
    SomeipLogAdapter::event().info("someip.event.published",
        "Remote command notification received from TSP",
        {tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("up")),
         tbox::fw::log::Field("command_id", tbox::fw::log::FieldValue::makeString(command.command_id)),
         tbox::fw::log::Field("command_type", tbox::fw::log::FieldValue::makeString(command.command_type)),
         tbox::fw::log::Field("payload_length", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(command.payload.size())),
             tbox::fw::log::Sensitivity::Payload),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("succeeded"))}
    );

    for (auto& callback : command_callbacks_) {
        if (callback) {
            callback(command);
        }
    }
}

void RemoteCmdRelayService::run() {
    while (running_) {
        // 处理远程命令
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace someip
} // namespace tbox
