#include "someip_daemon.h"
#include "tbox_someip/someip_log_adapter.h"
#include "tbox_someip/someip_context.h"

#include <csignal>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <chrono>

#ifdef USE_MOCK_SOMEIP
// Mock IPC clients for testing
namespace {

class MockRtcClient : public tbox::someip::IRtcClient {
public:
    bool initialize() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
    tbox::someip::ErrorCode getTime(tbox::someip::TimeData& time) override {
        time.is_valid = true;
        return tbox::someip::ErrorCode::SUCCESS;
    }
    void subscribeTimeUpdates(tbox::someip::TimeUpdateCallback callback) override {}
};

class MockGnssClient : public tbox::someip::IGnssClient {
public:
    bool initialize() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
    tbox::someip::ErrorCode getPosition(tbox::someip::PositionData& position) override {
        position.is_valid = true;
        return tbox::someip::ErrorCode::SUCCESS;
    }
    void subscribePositionUpdates(tbox::someip::PositionCallback callback) override {}
};

class MockTspClient : public tbox::someip::ITspClient {
public:
    bool initialize() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
    tbox::someip::ErrorCode getNetStatus(tbox::someip::NetStatusData& status) override {
        status.is_valid = true;
        return tbox::someip::ErrorCode::SUCCESS;
    }
    void subscribeNetStatusChanges(tbox::someip::NetStatusCallback callback) override {}
    void subscribeRemoteCommands(tbox::someip::RemoteCommandCallback callback) override {}
    tbox::someip::ErrorCode reportSoftwareInventory(const tbox::someip::VehicleSoftwareSnapshot& snapshot) override {
        return tbox::someip::ErrorCode::SUCCESS;
    }
    void subscribeFotaCommands(tbox::someip::FotaCommandCallback callback) override {}
};

class MockProvClient : public tbox::someip::IProvClient {
public:
    bool initialize() override { return true; }
    bool isConnected() const override { return true; }
    void shutdown() override {}
    std::string getConfiguration(const std::string& key) override { return ""; }
};

} // anonymous namespace
#endif // USE_MOCK_SOMEIP

namespace tbox {
namespace someip {

/**
 * @brief 解析 FOTA 软件库存上报的 payload
 *
 * Payload 格式（纯文本，| 分隔）：
 *   VIN|snapshot_seq|ecu_count|ecu_id_1,sw_version_1|ecu_id_2,sw_version_2|...
 *
 * 示例：
 *   12345678901234567|1|3|ECU_A,1.0.0|ECU_B,2.1.3|ECU_C,unknown
 *
 * @param payload 原始 payload 字节
 * @return 解析后的 VehicleSoftwareSnapshot
 * @throw std::runtime_error 解析失败时抛出异常
 */
static VehicleSoftwareSnapshot parseSoftwareInventoryPayload(const std::vector<uint8_t>& payload) {
    // 将 payload 转换为字符串
    std::string text(payload.begin(), payload.end());

    // 移除可能的尾部空白字符
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }

    std::istringstream iss(text);
    std::string token;
    std::vector<std::string> tokens;

    // 按 | 分割
    while (std::getline(iss, token, '|')) {
        tokens.push_back(token);
    }

    // 至少需要 VIN|snapshot_seq|ecu_count 三个字段
    if (tokens.size() < 3) {
        throw std::runtime_error("Invalid payload format: too few fields");
    }

    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = tokens[0];

    // 解析 snapshot_seq
    try {
        snapshot.snapshot_seq = std::stoull(tokens[1]);
    } catch (const std::exception& e) {
        throw std::runtime_error("Invalid snapshot_seq: " + tokens[1]);
    }

    // 解析 ecu_count
    size_t ecu_count;
    try {
        ecu_count = std::stoull(tokens[2]);
    } catch (const std::exception& e) {
        throw std::runtime_error("Invalid ecu_count: " + tokens[2]);
    }

    // 验证字段数量
    if (tokens.size() < 3 + ecu_count) {
        throw std::runtime_error("Payload truncated: expected " +
            std::to_string(3 + ecu_count) + " fields, got " + std::to_string(tokens.size()));
    }

    // 解析 ECU 列表
    for (size_t i = 0; i < ecu_count; ++i) {
        const std::string& ecu_entry = tokens[3 + i];

        // 按逗号分隔 ecu_id,sw_version
        size_t comma_pos = ecu_entry.find(',');
        if (comma_pos == std::string::npos) {
            throw std::runtime_error("Invalid ECU entry format (missing comma): " + ecu_entry);
        }

        EcuVersionEntry ecu;
        ecu.ecu_id = ecu_entry.substr(0, comma_pos);
        ecu.sw_version = ecu_entry.substr(comma_pos + 1);

        // 处理 "unknown" 版本
        if (ecu.sw_version.value() == "unknown") {
            ecu.sw_version = std::nullopt;
        }

        snapshot.ecu_list.push_back(std::move(ecu));
    }

    // 设置默认值
    snapshot.registry_version = "1.0";
    snapshot.collected_at = "";  // 可由调用方填充时间戳

    return snapshot;
}

SomeipDaemon* SomeipDaemon::instance_ = nullptr;

SomeipDaemon::SomeipDaemon() : running_(false) {
    instance_ = this;
}

SomeipDaemon::~SomeipDaemon() {
    shutdown();
    instance_ = nullptr;
}

bool SomeipDaemon::initialize(const ServiceConfig& config) {
    config_ = config;

    // 信号处理由 main.cpp 负责，此处不再注册

    SomeipLogAdapter::runtime().info("someip.runtime.initializing",
        "Initializing TBOX-SOMEIP daemon"
    );

    // 初始化 IPC 桥接层
    ipc_bridge_ = std::make_unique<IpcBridge>();
    if (!ipc_bridge_->initialize()) {
        SomeipLogAdapter::runtime().error("someip.runtime.ipc_bridge_init_failed",
            "Failed to initialize IPC bridge"
        );
        return false;
    }

    // 初始化 IPC 客户端
    if (!initializeIpcClients()) {
        SomeipLogAdapter::runtime().error("someip.runtime.ipc_clients_init_failed",
            "Failed to initialize IPC clients"
        );
        return false;
    }

    // 初始化服务
    if (!initializeServices()) {
        SomeipLogAdapter::runtime().error("someip.runtime.services_init_failed",
            "Failed to initialize services"
        );
        return false;
    }

    // 初始化服务发现
    service_discovery_ = std::make_unique<ServiceDiscovery>();
    if (!service_discovery_->initialize()) {
        SomeipLogAdapter::runtime().error("someip.runtime.sd_init_failed",
            "Failed to initialize service discovery"
        );
        return false;
    }

    SomeipLogAdapter::runtime().info("someip.runtime.initialized",
        "TBOX-SOMEIP daemon initialized successfully"
    );
    return true;
}

bool SomeipDaemon::start() {
    if (!ipc_bridge_ || !ipc_bridge_->isInitialized()) {
        SomeipLogAdapter::runtime().error("someip.runtime.start_failed",
            "Daemon not initialized"
        );
        return false;
    }

    SomeipLogAdapter::runtime().info("someip.runtime.starting",
        "Starting TBOX-SOMEIP daemon"
    );

    // 启动服务发现
    if (!startServiceDiscovery()) {
        SomeipLogAdapter::sd().error("someip.sd.start_failed",
            "Failed to start service discovery"
        );
        return false;
    }

    // 启动所有服务
    if (time_sync_service_) {
        if (!time_sync_service_->start()) {
            SomeipLogAdapter::runtime().error("someip.runtime.service_start_failed",
                "Failed to start TimeSync service",
                {tbox::fw::log::Field("service_name", tbox::fw::log::FieldValue::makeString("time_sync"))}
            );
            return false;
        }
    }

    if (location_service_) {
        if (!location_service_->start()) {
            SomeipLogAdapter::runtime().error("someip.runtime.service_start_failed",
                "Failed to start Location service",
                {tbox::fw::log::Field("service_name", tbox::fw::log::FieldValue::makeString("location"))}
            );
            return false;
        }
    }

    if (net_status_service_) {
        if (!net_status_service_->start()) {
            SomeipLogAdapter::runtime().error("someip.runtime.service_start_failed",
                "Failed to start NetStatus service",
                {tbox::fw::log::Field("service_name", tbox::fw::log::FieldValue::makeString("net_status"))}
            );
            return false;
        }
    }

    if (remote_cmd_relay_service_) {
        if (!remote_cmd_relay_service_->start()) {
            SomeipLogAdapter::runtime().error("someip.runtime.service_start_failed",
                "Failed to start RemoteCmdRelay service",
                {tbox::fw::log::Field("service_name", tbox::fw::log::FieldValue::makeString("remote_cmd_relay"))}
            );
            return false;
        }
    }

    if (fota_relay_service_) {
        if (!fota_relay_service_->start()) {
            SomeipLogAdapter::runtime().error("someip.runtime.service_start_failed",
                "Failed to start FotaRelay service",
                {tbox::fw::log::Field("service_name", tbox::fw::log::FieldValue::makeString("fota_relay"))}
            );
            return false;
        }
    }

    running_ = true;
    SomeipLogAdapter::runtime().info("someip.runtime.started",
        "TBOX-SOMEIP daemon started successfully"
    );

    return true;
}

void SomeipDaemon::shutdown() {
    if (!running_) {
        return;
    }

    SomeipLogAdapter::runtime().info("someip.runtime.shutting_down",
        "Shutting down TBOX-SOMEIP daemon..."
    );

    running_ = false;

    // 停止所有服务
    if (time_sync_service_) {
        time_sync_service_->stop();
    }

    if (location_service_) {
        location_service_->stop();
    }

    if (net_status_service_) {
        net_status_service_->stop();
    }

    if (remote_cmd_relay_service_) {
        remote_cmd_relay_service_->stop();
    }

    if (fota_relay_service_) {
        fota_relay_service_->stop();
    }

    // 停止服务发现
    if (service_discovery_) {
        service_discovery_->shutdown();
    }

    // 关闭 IPC 桥接层
    if (ipc_bridge_) {
        ipc_bridge_->shutdown();
    }

    SomeipLogAdapter::runtime().info("someip.runtime.stopped",
        "TBOX-SOMEIP daemon shutdown complete"
    );
}

bool SomeipDaemon::isRunning() const {
    return running_;
}

void SomeipDaemon::signalHandler(int signum) {
    // 信号处理由 main.cpp 负责，此方法保留兼容但不再使用
    (void)signum;
}

bool SomeipDaemon::initializeIpcClients() {
    SomeipLogAdapter::ipc_bridge().info("someip.ipc.initializing",
        "Initializing IPC clients..."
    );

#ifdef USE_MOCK_SOMEIP
    // Mock 模式：注册模拟的 IPC 客户端
    SomeipLogAdapter::ipc_bridge().info("someip.ipc.using_mock",
        "Using mock IPC clients"
    );

    ipc_bridge_->registerClient("rtc", std::make_shared<MockRtcClient>());
    ipc_bridge_->registerClient("gnss", std::make_shared<MockGnssClient>());
    ipc_bridge_->registerClient("tsp", std::make_shared<MockTspClient>());
    ipc_bridge_->registerClient("prov", std::make_shared<MockProvClient>());
#endif

    return true;
}

bool SomeipDaemon::initializeServices() {
    SomeipLogAdapter::runtime().info("someip.runtime.services_initializing",
        "Initializing services..."
    );

    // 初始化 TimeSync 服务
    if (config_.enable_time_sync) {
        time_sync_service_ = std::make_unique<TimeSyncService>(ipc_bridge_.get());
        if (!time_sync_service_->initialize()) {
            SomeipLogAdapter::runtime().error("someip.runtime.service_init_failed",
                "Failed to initialize TimeSync service",
                {tbox::fw::log::Field("service_name", tbox::fw::log::FieldValue::makeString("time_sync"))}
            );
            return false;
        }
    }

    // 初始化 Location 服务
    if (config_.enable_location) {
        location_service_ = std::make_unique<LocationService>(ipc_bridge_.get());
        if (!location_service_->initialize()) {
            SomeipLogAdapter::runtime().error("someip.runtime.service_init_failed",
                "Failed to initialize Location service",
                {tbox::fw::log::Field("service_name", tbox::fw::log::FieldValue::makeString("location"))}
            );
            return false;
        }
    }

    // 初始化 NetStatus 服务
    if (config_.enable_net_status) {
        net_status_service_ = std::make_unique<NetStatusService>(ipc_bridge_.get());
        if (!net_status_service_->initialize()) {
            SomeipLogAdapter::runtime().error("someip.runtime.service_init_failed",
                "Failed to initialize NetStatus service",
                {tbox::fw::log::Field("service_name", tbox::fw::log::FieldValue::makeString("net_status"))}
            );
            return false;
        }
    }

    // 初始化 RemoteCmdRelay 服务
    if (config_.enable_remote_cmd_relay) {
        remote_cmd_relay_service_ = std::make_unique<RemoteCmdRelayService>(ipc_bridge_.get());
        if (!remote_cmd_relay_service_->initialize()) {
            SomeipLogAdapter::runtime().error("someip.runtime.service_init_failed",
                "Failed to initialize RemoteCmdRelay service",
                {tbox::fw::log::Field("service_name", tbox::fw::log::FieldValue::makeString("remote_cmd_relay"))}
            );
            return false;
        }
    }

    // 初始化 FotaRelay 服务
    if (config_.enable_fota_relay) {
        fota_relay_service_ = std::make_unique<FotaRelayService>(ipc_bridge_.get());
        if (!fota_relay_service_->initialize()) {
            SomeipLogAdapter::runtime().error("someip.runtime.service_init_failed",
                "Failed to initialize FotaRelay service",
                {tbox::fw::log::Field("service_name", tbox::fw::log::FieldValue::makeString("fota_relay"))}
            );
            return false;
        }
    }

    SomeipLogAdapter::runtime().info("someip.runtime.services_initialized",
        "All services initialized"
    );
    return true;
}

bool SomeipDaemon::startServiceDiscovery() {
    if (!service_discovery_) {
        return false;
    }

    SomeipLogAdapter::sd().info("someip.sd.starting",
        "Starting service discovery..."
    );

    auto start_time = std::chrono::steady_clock::now();

    // 统一 offer TBOX-SOMEIP 聚合服务
    bool offer_result = service_discovery_->offerService(
        someip_addressing::SERVICE_ID,
        someip_addressing::INSTANCE_ID);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();

    if (!offer_result) {
        SomeipLogAdapter::sd().error("someip.sd.offer.failed",
            "Failed to offer TBOX-SOMEIP service",
            {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(someip_addressing::SERVICE_ID)),
             tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(someip_addressing::INSTANCE_ID)),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(duration)),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1001"))}
        );
        return false;
    }

    SomeipLogAdapter::sd().info("someip.sd.offer.succeeded",
        "TBOX-SOMEIP service offered successfully",
        {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(someip_addressing::SERVICE_ID)),
         tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(someip_addressing::INSTANCE_ID)),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("succeeded")),
         tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(duration))}
    );

    // 注册 FOTA Software Inventory 上报处理器
    service_discovery_->registerMethodHandler(
        method_ids::REPORT_SOFTWARE_INVENTORY,
        [this](const SomeIpMessage& request) -> SomeIpMessage {
            auto req_start_time = std::chrono::steady_clock::now();

            // 建立 SOME/IP Method 上下文
            std::string request_id = "someip-" + std::to_string(request.header.session_id);
            auto scope = make_context_scope("", request_id);

            SomeipLogAdapter::method().debug("someip.method.request.received",
                "Method request received",
                {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(request.header.service_id)),
                 tbox::fw::log::Field("someip_method_id", tbox::fw::log::FieldValue::makeInt(method_ids::REPORT_SOFTWARE_INVENTORY)),
                 tbox::fw::log::Field("someip_client_id", tbox::fw::log::FieldValue::makeInt(request.header.client_id)),
                 tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id))}
            );

            SomeIpMessage response;
            response.header.service_id = request.header.service_id;
            response.header.method_id = request.header.method_id;
            response.header.message_type = SomeIpMessageType::RESPONSE;
            response.header.client_id = request.header.client_id;
            response.header.session_id = request.header.session_id;
            response.header.protocol_version = request.header.protocol_version;
            response.header.interface_version = request.header.interface_version;

            // 检查 FotaRelayService 是否可用
            if (!fota_relay_service_) {
                SomeipLogAdapter::fota_relay().error("someip.fota.inventory.relayed",
                    "FotaRelayService not available",
                    {tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id)),
                     tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("up")),
                     tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
                     tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
                );
                response.header.return_code = SomeIpReturnCode::E_NOT_OK;

                auto req_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - req_start_time).count();
                SomeipLogAdapter::method().info("someip.method.request.completed",
                    "Method request completed",
                    {tbox::fw::log::Field("someip_method_id", tbox::fw::log::FieldValue::makeInt(method_ids::REPORT_SOFTWARE_INVENTORY)),
                     tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id)),
                     tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
                     tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(req_duration))}
                );
                return response;
            }

            // 解析 payload
            VehicleSoftwareSnapshot snapshot;
            try {
                snapshot = parseSoftwareInventoryPayload(request.payload);
            } catch (const std::exception& e) {
                SomeipLogAdapter::serializer().error("someip.mapping.failed",
                    "Failed to parse FOTA inventory payload",
                    {tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("deserialize")),
                     tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("up")),
                     tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
                     tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1004")),
                     tbox::fw::log::Field("payload_length", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(request.payload.size())))}
                );
                response.header.return_code = SomeIpReturnCode::E_NOT_OK;

                auto req_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - req_start_time).count();
                SomeipLogAdapter::method().info("someip.method.request.completed",
                    "Method request completed",
                    {tbox::fw::log::Field("someip_method_id", tbox::fw::log::FieldValue::makeInt(method_ids::REPORT_SOFTWARE_INVENTORY)),
                     tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id)),
                     tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
                     tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(req_duration)),
                     tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1004"))}
                );
                return response;
            }

            // 调用 FotaRelayService 上报（VIN 按 Identifier 脱敏）
            auto relay_start_time = std::chrono::steady_clock::now();
            ErrorCode result = fota_relay_service_->reportSoftwareInventory(snapshot);
            auto relay_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - relay_start_time).count();

            if (result == ErrorCode::SUCCESS) {
                response.header.return_code = SomeIpReturnCode::E_OK;

                SomeipLogAdapter::fota_relay().info("someip.fota.inventory.relayed",
                    "Software inventory relayed to TSP",
                    {tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id)),
                     tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("up")),
                     tbox::fw::log::Field("entry_count", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(snapshot.ecu_list.size()))),
                     tbox::fw::log::Field("snapshot_seq", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(snapshot.snapshot_seq))),
                     tbox::fw::log::Field("vin", tbox::fw::log::FieldValue::makeString(snapshot.vin),
                         tbox::fw::log::Sensitivity::Identifier),
                     tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("succeeded")),
                     tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(relay_duration))}
                );
            } else {
                response.header.return_code = SomeIpReturnCode::E_NOT_OK;

                std::string error_code_str = "TBOX-SOMEIP-1003";
                if (result == ErrorCode::CLIENT_CALL_TIMEOUT) {
                    error_code_str = "TBOX-SOMEIP-1002";
                }

                SomeipLogAdapter::fota_relay().error("someip.fota.inventory.relayed",
                    "Failed to relay software inventory to TSP",
                    {tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id)),
                     tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("up")),
                     tbox::fw::log::Field("entry_count", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(snapshot.ecu_list.size()))),
                     tbox::fw::log::Field("vin", tbox::fw::log::FieldValue::makeString(snapshot.vin),
                         tbox::fw::log::Sensitivity::Identifier),
                     tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
                     tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(relay_duration)),
                     tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString(error_code_str))}
                );
            }

            auto req_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - req_start_time).count();
            SomeipLogAdapter::method().info("someip.method.request.completed",
                "Method request completed",
                {tbox::fw::log::Field("someip_method_id", tbox::fw::log::FieldValue::makeInt(method_ids::REPORT_SOFTWARE_INVENTORY)),
                 tbox::fw::log::Field("someip_client_id", tbox::fw::log::FieldValue::makeInt(request.header.client_id)),
                 tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id)),
                 tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString(
                     result == ErrorCode::SUCCESS ? "succeeded" : "failed")),
                 tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(req_duration))}
            );

            return response;
        });

    // 订阅客户端连接事件
    service_discovery_->subscribeClientConnection(
        someip_addressing::SERVICE_ID,
        someip_addressing::INSTANCE_ID,
        [this](const ClientConnectionEvent& event) {
            handleClientConnection(event);
        });

    SomeipLogAdapter::sd().info("someip.sd.started",
        "Service discovery started"
    );
    return true;
}

void SomeipDaemon::handleClientConnection(const ClientConnectionEvent& event) {
    std::string result = event.connected ? "connected" : "disconnected";

    SomeipLogAdapter::sd().info("someip.sd.subscription.changed",
        "Client subscription changed",
        {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(event.service_id)),
         tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(event.instance_id)),
         tbox::fw::log::Field("someip_client_id", tbox::fw::log::FieldValue::makeInt(event.client_id)),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString(result))}
    );

    if (event.service_id == someip_addressing::SERVICE_ID &&
        event.instance_id == someip_addressing::INSTANCE_ID) {
        SomeipLogAdapter::fota_relay().info("someip.fota.client.changed",
            "CGW-FOTA client subscription changed",
            {tbox::fw::log::Field("someip_client_id", tbox::fw::log::FieldValue::makeInt(event.client_id)),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString(result))}
        );
    }
}

} // namespace someip
} // namespace tbox
