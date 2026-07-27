#include "location.h"
#include "ipc_bridge.h"
#include "tbox_someip/someip_log_adapter.h"

#include <chrono>

namespace tbox {
namespace someip {

LocationService::LocationService(IpcBridge* bridge)
    : bridge_(bridge)
    , state_(ServiceState::UNAVAILABLE)
    , running_(false) {}

LocationService::~LocationService() {
    stop();
}

bool LocationService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::UNAVAILABLE) {
        SomeipLogAdapter::method().warn("someip.location.already_initialized",
            "LocationService already initialized"
        );
        return false;
    }

    if (!bridge_ || !bridge_->isInitialized()) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "IpcBridge not initialized",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("gnss")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("initialize")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return false;
    }

    // 获取 GNSS 客户端
    auto client = bridge_->getClient("gnss");
    if (!client) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "GNSS client not found",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("gnss")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("get_client")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return false;
    }

    gnss_client_ = std::dynamic_pointer_cast<IGnssClient>(client);
    if (!gnss_client_) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "Invalid GNSS client type",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("gnss")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("cast")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return false;
    }

    // 初始化 GNSS 客户端
    if (!gnss_client_->initialize()) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "Failed to initialize GNSS client",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("gnss")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("initialize")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return false;
    }

    state_ = ServiceState::INITIALIZING;
    SomeipLogAdapter::method().info("someip.location.initialized",
        "LocationService initialized"
    );
    return true;
}

bool LocationService::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::INITIALIZING) {
        SomeipLogAdapter::method().warn("someip.location.start_failed",
            "LocationService not in initializing state"
        );
        return false;
    }

    // 启动工作线程
    running_ = true;
    worker_thread_ = std::thread(&LocationService::run, this);

    state_ = ServiceState::AVAILABLE;
    SomeipLogAdapter::method().info("someip.location.started",
        "LocationService started"
    );
    return true;
}

void LocationService::stop() {
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

    if (gnss_client_) {
        gnss_client_->shutdown();
        gnss_client_.reset();
    }

    position_callbacks_.clear();
    state_ = ServiceState::UNAVAILABLE;

    SomeipLogAdapter::method().info("someip.location.stopped",
        "LocationService stopped"
    );
}

ServiceState LocationService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

uint16_t LocationService::getServiceId() const {
    return someip_addressing::SERVICE_ID;
}

uint16_t LocationService::getInstanceId() const {
    return someip_addressing::INSTANCE_ID;
}

ErrorCode LocationService::getPosition(PositionData& position) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::AVAILABLE) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "LocationService not available",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("gnss")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("get_position")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return ErrorCode::CLIENT_CALL_FAILED;
    }

    if (!gnss_client_ || !gnss_client_->isConnected()) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "GNSS client not connected",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("gnss")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("get_position")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return ErrorCode::CLIENT_CALL_FAILED;
    }

    return gnss_client_->getPosition(position);
}

void LocationService::subscribePositionUpdates(PositionCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    position_callbacks_.push_back(callback);

    // 订阅 GNSS 位置更新
    if (gnss_client_) {
        gnss_client_->subscribePositionUpdates([this](const PositionData& position) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& cb : position_callbacks_) {
                if (cb) {
                    cb(position);
                }
            }
        });
    }
}

void LocationService::run() {
    while (running_) {
        // 定期更新位置
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace someip
} // namespace tbox
