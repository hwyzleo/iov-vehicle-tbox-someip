#include "time_sync.h"
#include "ipc_bridge.h"
#include "tbox_someip/someip_log_adapter.h"

#include <chrono>

namespace tbox {
namespace someip {

TimeSyncService::TimeSyncService(IpcBridge* bridge)
    : bridge_(bridge)
    , state_(ServiceState::UNAVAILABLE)
    , running_(false) {}

TimeSyncService::~TimeSyncService() {
    stop();
}

bool TimeSyncService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::UNAVAILABLE) {
        SomeipLogAdapter::method().warn("someip.time_sync.already_initialized",
            "TimeSyncService already initialized"
        );
        return false;
    }

    if (!bridge_ || !bridge_->isInitialized()) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "IpcBridge not initialized",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("rtc")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("initialize")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return false;
    }

    // 获取 RTC 客户端
    auto client = bridge_->getClient("rtc");
    if (!client) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "RTC client not found",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("rtc")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("get_client")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return false;
    }

    rtc_client_ = std::dynamic_pointer_cast<IRtcClient>(client);
    if (!rtc_client_) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "Invalid RTC client type",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("rtc")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("cast")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return false;
    }

    // 初始化 RTC 客户端
    if (!rtc_client_->initialize()) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "Failed to initialize RTC client",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("rtc")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("initialize")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return false;
    }

    state_ = ServiceState::INITIALIZING;
    SomeipLogAdapter::method().info("someip.time_sync.initialized",
        "TimeSyncService initialized"
    );
    return true;
}

bool TimeSyncService::start() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::INITIALIZING) {
        SomeipLogAdapter::method().warn("someip.time_sync.start_failed",
            "TimeSyncService not in initializing state"
        );
        return false;
    }

    // 启动工作线程
    running_ = true;
    worker_thread_ = std::thread(&TimeSyncService::run, this);

    state_ = ServiceState::AVAILABLE;
    SomeipLogAdapter::method().info("someip.time_sync.started",
        "TimeSyncService started"
    );
    return true;
}

void TimeSyncService::stop() {
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

    if (rtc_client_) {
        rtc_client_->shutdown();
        rtc_client_.reset();
    }

    time_callbacks_.clear();
    state_ = ServiceState::UNAVAILABLE;

    SomeipLogAdapter::method().info("someip.time_sync.stopped",
        "TimeSyncService stopped"
    );
}

ServiceState TimeSyncService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

uint16_t TimeSyncService::getServiceId() const {
    return someip_addressing::SERVICE_ID;
}

uint16_t TimeSyncService::getInstanceId() const {
    return someip_addressing::INSTANCE_ID;
}

ErrorCode TimeSyncService::getTime(TimeData& time) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (state_ != ServiceState::AVAILABLE) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "TimeSyncService not available",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("rtc")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("get_time")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return ErrorCode::CLIENT_CALL_FAILED;
    }

    if (!rtc_client_ || !rtc_client_->isConnected()) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "RTC client not connected",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("rtc")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("get_time")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return ErrorCode::CLIENT_CALL_FAILED;
    }

    return rtc_client_->getTime(time);
}

void TimeSyncService::subscribeTimeUpdates(TimeUpdateCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    time_callbacks_.push_back(callback);

    // 订阅 RTC 时间更新
    if (rtc_client_) {
        rtc_client_->subscribeTimeUpdates([this](const TimeData& time) {
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto& cb : time_callbacks_) {
                if (cb) {
                    cb(time);
                }
            }
        });
    }
}

void TimeSyncService::run() {
    while (running_) {
        // 定期更新时间
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace someip
} // namespace tbox
