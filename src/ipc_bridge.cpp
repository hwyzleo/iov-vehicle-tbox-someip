#include "ipc_bridge.h"
#include "tbox_someip/someip_log_adapter.h"

namespace tbox {
namespace someip {

IpcBridge::IpcBridge() : initialized_(false) {}

IpcBridge::~IpcBridge() {
    shutdown();
}

bool IpcBridge::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        SomeipLogAdapter::ipc_bridge().warn("someip.ipc.already_initialized",
            "IpcBridge already initialized"
        );
        return false;
    }

    // 初始化 IPC 桥接层
    // TODO: 实现实际的 IPC 初始化逻辑

    initialized_ = true;
    SomeipLogAdapter::ipc_bridge().info("someip.ipc.initialized",
        "IpcBridge initialized successfully"
    );
    return true;
}

bool IpcBridge::registerClient(const std::string& name, std::shared_ptr<IIpcClient> client) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        SomeipLogAdapter::ipc_bridge().warn("someip.ipc.call.failed",
            "IpcBridge not initialized",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString(name)),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("register")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed"))}
        );
        return false;
    }

    if (!client) {
        SomeipLogAdapter::ipc_bridge().warn("someip.ipc.call.failed",
            "Invalid client pointer",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString(name)),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("register")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed"))}
        );
        return false;
    }

    if (clients_.find(name) != clients_.end()) {
        SomeipLogAdapter::ipc_bridge().warn("someip.ipc.call.failed",
            "Client already registered",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString(name)),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("register")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed"))}
        );
        return false;
    }

    clients_[name] = client;
    SomeipLogAdapter::ipc_bridge().info("someip.ipc.client_registered",
        "Client registered successfully",
        {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString(name)),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("succeeded"))}
    );
    return true;
}

std::shared_ptr<IIpcClient> IpcBridge::getClient(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        SomeipLogAdapter::ipc_bridge().warn("someip.ipc.call.failed",
            "IpcBridge not initialized",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString(name)),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("get_client")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return nullptr;
    }

    auto it = clients_.find(name);
    if (it != clients_.end()) {
        return it->second;
    }

    SomeipLogAdapter::ipc_bridge().warn("someip.ipc.call.failed",
        "Client not found",
        {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString(name)),
         tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("get_client")),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
         tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
    );
    return nullptr;
}

void IpcBridge::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return;
    }

    // 关闭所有客户端
    for (auto& pair : clients_) {
        if (pair.second) {
            pair.second->shutdown();
        }
    }
    clients_.clear();

    initialized_ = false;
    SomeipLogAdapter::ipc_bridge().info("someip.ipc.shutdown",
        "IpcBridge shutdown"
    );
}

bool IpcBridge::isInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

} // namespace someip
} // namespace tbox
