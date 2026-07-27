#include "service_discovery.h"
#include "tbox_someip/someip_types.h"
#include "tbox_someip/someip_log_adapter.h"

#include <chrono>

namespace tbox {
namespace someip {

ServiceDiscovery::ServiceDiscovery() : initialized_(false) {}

ServiceDiscovery::~ServiceDiscovery() {
    shutdown();
}

bool ServiceDiscovery::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (initialized_) {
        SomeipLogAdapter::sd().warn("someip.sd.already_initialized",
            "ServiceDiscovery already initialized"
        );
        return false;
    }

    // 创建 SOME/IP 服务
    someip_service_ = createSomeIpService();
    if (!someip_service_) {
        SomeipLogAdapter::sd().error("someip.sd.init_failed",
            "Failed to create SomeIpService",
            {tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1001"))}
        );
        return false;
    }

    initialized_ = true;
    SomeipLogAdapter::sd().info("someip.sd.initialized",
        "ServiceDiscovery initialized successfully"
    );
    return true;
}

bool ServiceDiscovery::offerService(uint16_t service_id, uint16_t instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        SomeipLogAdapter::sd().error("someip.sd.offer.failed",
            "ServiceDiscovery not initialized",
            {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(service_id)),
             tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(instance_id)),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1001"))}
        );
        return false;
    }

    auto start_time = std::chrono::steady_clock::now();

    // 配置 SOME/IP 服务
    SomeIpServiceConfig config;
    config.service_id = service_id;
    config.instance_id = instance_id;
    config.ip_address = "0.0.0.0";
    config.port = 56101; // 默认端口

    // 初始化并启动 SOME/IP 服务
    if (!someip_service_->initialize(config)) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        SomeipLogAdapter::sd().error("someip.sd.offer.failed",
            "Failed to initialize SomeIpService",
            {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(service_id)),
             tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(instance_id)),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(duration)),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1001"))}
        );
        return false;
    }

    if (!someip_service_->start()) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        SomeipLogAdapter::sd().error("someip.sd.offer.failed",
            "Failed to start SomeIpService",
            {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(service_id)),
             tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(instance_id)),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(duration)),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1001"))}
        );
        return false;
    }

    if (!someip_service_->offerService()) {
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        SomeipLogAdapter::sd().error("someip.sd.offer.failed",
            "Failed to offer SomeIpService",
            {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(service_id)),
             tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(instance_id)),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(duration)),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1001"))}
        );
        return false;
    }

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
    SomeipLogAdapter::sd().info("someip.sd.offer.succeeded",
        "Service offered successfully",
        {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(service_id)),
         tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(instance_id)),
         tbox::fw::log::Field("port", tbox::fw::log::FieldValue::makeInt(someip_service_->getBoundPort())),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("succeeded")),
         tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(duration))}
    );
    return true;
}

void ServiceDiscovery::stopOfferService(uint16_t service_id, uint16_t instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !someip_service_) {
        return;
    }

    // 停止发布服务
    someip_service_->stopOfferService();
    someip_service_->stop();

    SomeipLogAdapter::sd().info("someip.sd.offer.stopped",
        "Stopped offering service",
        {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(service_id)),
         tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(instance_id))}
    );
}

void ServiceDiscovery::subscribeServiceAvailability(
    uint16_t service_id,
    uint16_t instance_id,
    std::function<void(bool)> callback) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        SomeipLogAdapter::sd().warn("someip.sd.subscribe.failed",
            "ServiceDiscovery not initialized for availability subscription",
            {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(service_id)),
             tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(instance_id))}
        );
        return;
    }

    // 保存回调
    availability_callbacks_[{service_id, instance_id}] = callback;

    SomeipLogAdapter::sd().info("someip.sd.subscription.changed",
        "Subscribed to service availability",
        {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(service_id)),
         tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(instance_id)),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("subscribed"))}
    );
}

void ServiceDiscovery::subscribeClientConnection(
    uint16_t service_id,
    uint16_t instance_id,
    ClientConnectionCallback callback) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        SomeipLogAdapter::sd().warn("someip.sd.subscribe.failed",
            "ServiceDiscovery not initialized for client connection subscription",
            {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(service_id)),
             tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(instance_id))}
        );
        return;
    }

    // 保存回调
    client_connection_callbacks_[{service_id, instance_id}] = callback;

    SomeipLogAdapter::sd().info("someip.sd.subscription.changed",
        "Subscribed to client connection events",
        {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(service_id)),
         tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(instance_id)),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("subscribed"))}
    );
}

void ServiceDiscovery::simulateClientConnection(
    uint16_t service_id, uint16_t instance_id, uint16_t client_id, bool connected) {

    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return;
    }

    auto it = client_connection_callbacks_.find({service_id, instance_id});
    if (it != client_connection_callbacks_.end()) {
        ClientConnectionEvent event;
        event.service_id = service_id;
        event.instance_id = instance_id;
        event.client_id = client_id;
        event.connected = connected;
        event.timestamp = std::chrono::system_clock::now();

        // 保存回调，然后在锁外调用
        auto callback = it->second;
        lock.~lock_guard();
        callback(event);
    }
}

void ServiceDiscovery::registerMethodHandler(MethodId method_id, SomeIpRequestHandler handler) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || !someip_service_) {
        SomeipLogAdapter::sd().error("someip.sd.method_register.failed",
            "ServiceDiscovery not initialized or SomeIpService not available",
            {tbox::fw::log::Field("someip_method_id", tbox::fw::log::FieldValue::makeInt(method_id))}
        );
        return;
    }

    someip_service_->registerMethodHandler(method_id, std::move(handler));
    SomeipLogAdapter::sd().info("someip.sd.method_registered",
        "Method handler registered",
        {tbox::fw::log::Field("someip_method_id", tbox::fw::log::FieldValue::makeInt(method_id))}
    );
}

void ServiceDiscovery::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return;
    }

    // 停止 SOME/IP 服务
    if (someip_service_) {
        someip_service_->stopOfferService();
        someip_service_->stop();
        someip_service_.reset();
    }

    availability_callbacks_.clear();
    client_connection_callbacks_.clear();
    initialized_ = false;

    SomeipLogAdapter::sd().info("someip.sd.shutdown",
        "ServiceDiscovery shutdown"
    );
}

} // namespace someip
} // namespace tbox
