#include "someip_daemon.h"
#include "tbox_someip/someip_log_adapter.h"
#include "tbox_someip/someip_context.h"

#include "bridge/tsp_client_adapter.h"
#include "bridge/prov_client_adapter.h"
#include "bridge/vehicle_message_validator.h"

#include <csignal>
#include <string>
#include <vector>
#include <chrono>

namespace tbox {
namespace someip {

namespace {

// 通用车云消息 wire：SOME/IP payload（uint8）<-> Envelope bytes（std::byte）
std::vector<std::byte> toBytes(const std::vector<uint8_t>& v) {
    std::vector<std::byte> out(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        out[i] = static_cast<std::byte>(v[i]);
    }
    return out;
}

std::vector<uint8_t> toU8(const std::vector<std::byte>& v) {
    std::vector<uint8_t> out(v.size());
    for (size_t i = 0; i < v.size(); ++i) {
        out[i] = static_cast<uint8_t>(v[i]);
    }
    return out;
}

} // namespace

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

    if (vehicle_message_gateway_) {
        if (!vehicle_message_gateway_->start()) {
            SomeipLogAdapter::runtime().error("someip.runtime.service_start_failed",
                "Failed to start VehicleMessageGateway service",
                {tbox::fw::log::Field("service_name", tbox::fw::log::FieldValue::makeString("vehicle_message"))}
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

    if (vehicle_message_gateway_) {
        vehicle_message_gateway_->stop();
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

    // TSP: 通用车云消息上行/下行链路走 tbox::tsp_client（CR-005/CR-008/配对 TSP CR）
    TspAdapterConfig tsp_cfg;
    tsp_cfg.socket_path = config_.tsp.socket_path;
    tsp_cfg.default_timeout_ms = config_.bridge_request_deadline_ms;
    tsp_cfg.event_queue_size = config_.bridge_queue_capacity;
    // ExchangeOptions 上送 TSP（须 < SOME/IP Method deadline，CR-008）
    tsp_cfg.exchange_timeout_ms = config_.generic_message.tsp_timeout_ms;
    tsp_cfg.max_response_bytes = config_.generic_message.limits.max_envelope_bytes;
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

    // 初始化 VehicleMessageGateway（CR-008 通用车云消息门面，取代旧 FOTA 中继）
    if (config_.enable_vehicle_message_gateway) {
        GenericMessageConfig vm_cfg = config_.generic_message;

        vehicle_message_gateway_ = std::make_unique<VehicleMessageGateway>(
            ipc_bridge_.get(), config_.service_id, config_.instance_id, vm_cfg);
        if (!vehicle_message_gateway_->initialize()) {
            SomeipLogAdapter::runtime().error("someip.runtime.service_init_failed",
                "Failed to initialize VehicleMessageGateway service",
                {tbox::fw::log::Field("service_name", tbox::fw::log::FieldValue::makeString("vehicle_message"))}
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

    // 注册通用车云消息 Method（METHOD_EXCHANGE_VEHICLE_MESSAGE，CR-008）。
    // 精确 Method ID 由 Registry 分配（US-011）；未分配前不注册（延迟 Offer），
    // 不猜测/占用未批准数值。
    if (config_.method_exchange_vehicle_message_id.has_value()) {
        const uint16_t kMethodId = config_.method_exchange_vehicle_message_id.value();
        service_discovery_->registerMethodHandler(kMethodId,
            [this, kMethodId](const SomeIpMessage& request) -> SomeIpMessage {
                auto req_start_time = std::chrono::steady_clock::now();
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

                if (!vehicle_message_gateway_) {
                    response.header.return_code = SomeIpReturnCode::E_NOT_OK;
                    SomeipLogAdapter::vehicle_message().error("someip.vehicle_message.request.rejected",
                        "VehicleMessageGateway not available",
                        {tbox::fw::log::Field("request_id", tbox::fw::log::FieldValue::makeString(request_id)),
                         tbox::fw::log::Field("outcome", tbox::fw::log::FieldValue::makeString("unavailable")),
                         tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
                    );
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

                // 构造 wire frame：单一序列化 Envelope，payload 透明（CR-008 US-012）
                VehicleMessageFrame frame;
                frame.envelope_bytes = toBytes(request.payload);

                // 通用 exchange：校验 → in-flight → TSP 桥接 → 关联校验
                VehicleMessageResponseFrame result =
                    vehicle_message_gateway_->exchangeVehicleMessage(frame);

                const bool accepted = (result.outcome == TransportOutcome::Accepted);
                response.header.return_code =
                    accepted ? SomeIpReturnCode::E_OK : SomeIpReturnCode::E_NOT_OK;

                std::string result_str;
                switch (result.outcome) {
                    case TransportOutcome::Accepted:      result_str = "accepted"; break;
                    case TransportOutcome::Rejected:      result_str = "rejected"; break;
                    case TransportOutcome::Timeout:       result_str = "timeout"; break;
                    case TransportOutcome::Unknown:       result_str = "unknown"; break;
                    case TransportOutcome::Unavailable:   result_str = "unavailable"; break;
                    case TransportOutcome::VersionMismatch: result_str = "version_mismatch"; break;
                    case TransportOutcome::PayloadTooLarge: result_str = "payload_too_large"; break;
                    case TransportOutcome::ProtocolError: result_str = "protocol_error"; break;
                    case TransportOutcome::Stopping:      result_str = "stopping"; break;
                }

                // 响应 payload：outcome + error_code + 可选业务 RESPONSE Envelope
                response.payload = encodeResponseFrame(result);

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
        SomeipLogAdapter::sd().info("someip.sd.method_registered",
            "Generic vehicle message method registered",
            {tbox::fw::log::Field("someip_method_id", tbox::fw::log::FieldValue::makeInt(kMethodId))}
        );
    } else {
        // Registry 未分配前不注册（延迟 Offer；US-011 不猜测未批准数值）
        SomeipLogAdapter::sd().info("someip.sd.method_register.deferred",
            "Generic vehicle message method not registered: ID pending Registry assignment",
            {}
        );
    }

    // 注册通用车云消息 Event（EVENT_VEHICLE_MESSAGE 下行）并接线下行通知
    if (config_.event_vehicle_message_id.has_value() && vehicle_message_gateway_) {
        const uint16_t kEventId = config_.event_vehicle_message_id.value();
        service_discovery_->registerEventHandler(kEventId,
            [](const SomeIpMessage& notification) {
                // 订阅方处理；当前无内部消费者
                SomeipLogAdapter::event().debug("someip.event.notification.received",
                    "Vehicle message event notification received",
                    {tbox::fw::log::Field("someip_event_id", tbox::fw::log::FieldValue::makeInt(
                         notification.header.method_id))}
                );
            });
        vehicle_message_gateway_->setDownlinkHandler(
            [this, kEventId](const VehicleMessageFrame& downlink) {
                // 下行：bridge worker -> EVENT_VEHICLE_MESSAGE 通知（保持顺序）
                SomeIpMessage notification;
                notification.header.service_id = config_.service_id;
                notification.header.method_id = kEventId;
                notification.header.message_type = SomeIpMessageType::NOTIFICATION;
                notification.payload = toU8(downlink.envelope_bytes);
                service_discovery_->notifyEvent(kEventId, notification);
            });
        SomeipLogAdapter::sd().info("someip.sd.event_registered",
            "Generic vehicle message event registered",
            {tbox::fw::log::Field("someip_event_id", tbox::fw::log::FieldValue::makeInt(kEventId))}
        );
    } else {
        SomeipLogAdapter::sd().info("someip.sd.event_register.deferred",
            "Generic vehicle message event not registered: ID pending Registry assignment",
            {}
        );
    }

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
        SomeipLogAdapter::vehicle_message().info("someip.vehicle_message.client.changed",
            "CGW client subscription changed",
            {tbox::fw::log::Field("someip_client_id", tbox::fw::log::FieldValue::makeInt(event.client_id)),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString(result))}
        );
    }
}

} // namespace someip
} // namespace tbox
