#include "someip_daemon.h"
#include "tbox_someip/someip_log_adapter.h"
#include "tbox_someip/someip_context.h"

#include "bridge/tsp_client_adapter.h"
#include "bridge/prov_client_adapter.h"

#include <csignal>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>
#include <chrono>

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

    // 初始化服务发现（CR-007 方案 C：port 从 config 读取）
    service_discovery_ = std::make_unique<ServiceDiscovery>(config_.port);
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

    // 门面服务始终通过真实 framework-ipc 域 facade 接入后端 TSP/PROV。
    // 后端 daemon 未就绪不阻塞启动：注册后按需重连（client 含重连重试）。
    // RTC/GNSS 暂未提供后端，未注册。
    SomeipLogAdapter::ipc_bridge().info("someip.ipc.using_real",
        "Using framework-ipc domain client facades (tsp/prov)"
    );

    // TSP: FOTA 上行/下行链路走 tbox::tsp_client (CR-005)
    TspAdapterConfig tsp_cfg;
    tsp_cfg.socket_path = config_.tsp.socket_path;
    tsp_cfg.default_timeout_ms = config_.bridge_request_deadline_ms;
    tsp_cfg.event_queue_size = config_.bridge_queue_capacity;
    auto tsp_adapter = std::make_shared<TspClientAdapter>(tsp_cfg);
    if (!tsp_adapter->initialize()) {
        // 非致命：后端 daemon 可能尚未就绪。门面常驻服务照常启动，
        // framework-ipc client 会在后续调用时按需重连（含一次重连重试）。
        SomeipLogAdapter::ipc_bridge().warn("someip.ipc.tsp.connect_deferred",
            "TSP daemon not reachable yet; registered adapter, will reconnect on demand",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("tsp")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("deferred"))}
        );
    }
    ipc_bridge_->registerClient("tsp", tsp_adapter);

    // PROV: 配置/绑定只读链路走 tbox::prov_client (CR-006)
    ProvAdapterConfig prov_cfg;
    prov_cfg.socket_path = config_.prov.socket_path;
    prov_cfg.connect_timeout_ms = config_.prov.connect_timeout_ms;
    auto prov_adapter = std::make_shared<ProvClientAdapter>(prov_cfg);
    if (!prov_adapter->initialize()) {
        // 非致命：同 TSP，按需重连。
        SomeipLogAdapter::ipc_bridge().warn("someip.ipc.prov.connect_deferred",
            "PROV daemon not reachable yet; registered adapter, will reconnect on demand",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("prov")),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("deferred"))}
        );
    }
    ipc_bridge_->registerClient("prov", prov_adapter);

    return true;
}

bool SomeipDaemon::initializeServices() {
    SomeipLogAdapter::runtime().info("someip.runtime.services_initializing",
        "Initializing services..."
    );

    // 初始化 NetStatus 服务
    if (config_.enable_net_status) {
        net_status_service_ = std::make_unique<NetStatusService>(ipc_bridge_.get(), config_.service_id, config_.instance_id);
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
        remote_cmd_relay_service_ = std::make_unique<RemoteCmdRelayService>(ipc_bridge_.get(), config_.service_id, config_.instance_id);
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
        fota_relay_service_ = std::make_unique<FotaRelayService>(ipc_bridge_.get(), config_.service_id, config_.instance_id);
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
        config_.service_id,
        config_.instance_id);

    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();

    if (!offer_result) {
        SomeipLogAdapter::sd().error("someip.sd.offer.failed",
            "Failed to offer TBOX-SOMEIP service",
            {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(config_.service_id)),
             tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(config_.instance_id)),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(duration)),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1001"))}
        );
        return false;
    }

    SomeipLogAdapter::sd().info("someip.sd.offer.succeeded",
        "TBOX-SOMEIP service offered successfully",
        {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(config_.service_id)),
         tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(config_.instance_id)),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("succeeded")),
         tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(duration))}
    );

    // 注册 FOTA Software Inventory 上报处理器
    service_discovery_->registerMethodHandler(
        config_.method_report_software_inventory_id,
        [this](const SomeIpMessage& request) -> SomeIpMessage {
            const uint16_t kMethodId = config_.method_report_software_inventory_id;
            auto req_start_time = std::chrono::steady_clock::now();

            // 建立 SOME/IP Method 上下文
            std::string request_id = "someip-" + std::to_string(request.header.session_id);
            auto scope = make_context_scope("", request_id);

            SomeipLogAdapter::method().debug("someip.method.request.received",
                "Method request received",
                {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(request.header.service_id)),
                 tbox::fw::log::Field("someip_method_id", tbox::fw::log::FieldValue::makeInt(kMethodId)),
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
                    {tbox::fw::log::Field("someip_method_id", tbox::fw::log::FieldValue::makeInt(kMethodId)),
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
                    {tbox::fw::log::Field("someip_method_id", tbox::fw::log::FieldValue::makeInt(kMethodId)),
                     tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id)),
                     tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
                     tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(req_duration)),
                     tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1004"))}
                );
                return response;
            }

            // 调用 FotaRelayService 上报（VIN 按 Identifier 脱敏）
            auto relay_start_time = std::chrono::steady_clock::now();
            BridgeCallResult result = fota_relay_service_->reportSoftwareInventory(snapshot);
            auto relay_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - relay_start_time).count();

            std::string result_str;
            switch (result.outcome) {
                case BridgeOutcome::SUCCESS: result_str = "succeeded"; break;
                case BridgeOutcome::UNKNOWN: result_str = "unknown";   break;
                default:                     result_str = "failed";    break;
            }

            if (result.outcome == BridgeOutcome::SUCCESS) {
                response.header.return_code = SomeIpReturnCode::E_OK;

                SomeipLogAdapter::fota_relay().info("someip.fota.inventory.relayed",
                    "Software inventory relayed to TSP",
                    {tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id)),
                     tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("up")),
                     tbox::fw::log::Field("entry_count", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(snapshot.ecu_list.size()))),
                     tbox::fw::log::Field("snapshot_seq", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(snapshot.snapshot_seq))),
                     tbox::fw::log::Field("vin", tbox::fw::log::FieldValue::makeString(snapshot.vin),
                         tbox::fw::log::Sensitivity::Identifier),
                     tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString(result_str)),
                     tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(relay_duration))}
                );
            } else {
                response.header.return_code = SomeIpReturnCode::E_NOT_OK;

                std::string error_code_str = "TBOX-SOMEIP-1003";

                SomeipLogAdapter::fota_relay().error("someip.fota.inventory.relayed",
                    "Failed to relay software inventory to TSP",
                    {tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id)),
                     tbox::fw::log::Field("direction", tbox::fw::log::FieldValue::makeString("up")),
                     tbox::fw::log::Field("entry_count", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(snapshot.ecu_list.size()))),
                     tbox::fw::log::Field("snapshot_seq", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(snapshot.snapshot_seq))),
                     tbox::fw::log::Field("vin", tbox::fw::log::FieldValue::makeString(snapshot.vin),
                         tbox::fw::log::Sensitivity::Identifier),
                     tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString(result_str)),
                     tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(relay_duration)),
                     tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString(error_code_str))}
                );
            }

            auto req_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - req_start_time).count();
            SomeipLogAdapter::method().info("someip.method.request.completed",
                "Method request completed",
                {tbox::fw::log::Field("someip_method_id", tbox::fw::log::FieldValue::makeInt(kMethodId)),
                 tbox::fw::log::Field("someip_client_id", tbox::fw::log::FieldValue::makeInt(request.header.client_id)),
                 tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id)),
                 tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString(result_str)),
                 tbox::fw::log::Field("duration_ms", tbox::fw::log::FieldValue::makeInt(req_duration))}
            );

            return response;
        });

    // 订阅客户端连接事件
    service_discovery_->subscribeClientConnection(
        config_.service_id,
        config_.instance_id,
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

    if (event.service_id == config_.service_id &&
        event.instance_id == config_.instance_id) {
        SomeipLogAdapter::fota_relay().info("someip.fota.client.changed",
            "CGW-FOTA client subscription changed",
            {tbox::fw::log::Field("someip_client_id", tbox::fw::log::FieldValue::makeInt(event.client_id)),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString(result))}
        );
    }
}

} // namespace someip
} // namespace tbox
