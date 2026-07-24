#include "someip_daemon.h"
#include <iostream>
#include <csignal>
#include <sstream>
#include <string>
#include <vector>
#include <stdexcept>

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
    
    std::cout << "Initializing TBOX-SOMEIP daemon..." << std::endl;
    
    // 设置信号处理
    signal(SIGINT, SomeipDaemon::signalHandler);
    signal(SIGTERM, SomeipDaemon::signalHandler);
    
    // 初始化 IPC 桥接层
    ipc_bridge_ = std::make_unique<IpcBridge>();
    if (!ipc_bridge_->initialize()) {
        std::cerr << "Failed to initialize IPC bridge" << std::endl;
        return false;
    }
    
    // 初始化 IPC 客户端
    if (!initializeIpcClients()) {
        std::cerr << "Failed to initialize IPC clients" << std::endl;
        return false;
    }
    
    // 初始化服务
    if (!initializeServices()) {
        std::cerr << "Failed to initialize services" << std::endl;
        return false;
    }
    
    // 初始化服务发现
    service_discovery_ = std::make_unique<ServiceDiscovery>();
    if (!service_discovery_->initialize()) {
        std::cerr << "Failed to initialize service discovery" << std::endl;
        return false;
    }
    
    std::cout << "TBOX-SOMEIP daemon initialized successfully" << std::endl;
    return true;
}

bool SomeipDaemon::start() {
    if (!ipc_bridge_ || !ipc_bridge_->isInitialized()) {
        std::cerr << "Daemon not initialized" << std::endl;
        return false;
    }
    
    std::cout << "Starting TBOX-SOMEIP daemon..." << std::endl;
    
    // 启动服务发现
    if (!startServiceDiscovery()) {
        std::cerr << "Failed to start service discovery" << std::endl;
        return false;
    }
    
    // 启动所有服务
    if (time_sync_service_) {
        if (!time_sync_service_->start()) {
            std::cerr << "Failed to start TimeSync service" << std::endl;
            return false;
        }
    }
    
    if (location_service_) {
        if (!location_service_->start()) {
            std::cerr << "Failed to start Location service" << std::endl;
            return false;
        }
    }
    
    if (net_status_service_) {
        if (!net_status_service_->start()) {
            std::cerr << "Failed to start NetStatus service" << std::endl;
            return false;
        }
    }
    
    if (remote_cmd_relay_service_) {
        if (!remote_cmd_relay_service_->start()) {
            std::cerr << "Failed to start RemoteCmdRelay service" << std::endl;
            return false;
        }
    }
    
    if (fota_relay_service_) {
        if (!fota_relay_service_->start()) {
            std::cerr << "Failed to start FotaRelay service" << std::endl;
            return false;
        }
    }
    
    running_ = true;
    std::cout << "TBOX-SOMEIP daemon started successfully" << std::endl;
    

    
    return true;
}

void SomeipDaemon::shutdown() {
    if (!running_) {
        return;
    }
    
    std::cout << "Shutting down TBOX-SOMEIP daemon..." << std::endl;
    
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
    
    std::cout << "TBOX-SOMEIP daemon shutdown complete" << std::endl;
}

bool SomeipDaemon::isRunning() const {
    return running_;
}

void SomeipDaemon::signalHandler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received." << std::endl;
    
    if (instance_) {
        instance_->shutdown();
    }
    
    exit(signum);
}

bool SomeipDaemon::initializeIpcClients() {
    std::cout << "Initializing IPC clients..." << std::endl;
    
#ifdef USE_MOCK_SOMEIP
    // Mock 模式：注册模拟的 IPC 客户端
    std::cout << "Using mock IPC clients" << std::endl;
    
    ipc_bridge_->registerClient("rtc", std::make_shared<MockRtcClient>());
    ipc_bridge_->registerClient("gnss", std::make_shared<MockGnssClient>());
    ipc_bridge_->registerClient("tsp", std::make_shared<MockTspClient>());
    ipc_bridge_->registerClient("prov", std::make_shared<MockProvClient>());
#endif
    
    return true;
}

bool SomeipDaemon::initializeServices() {
    std::cout << "Initializing services..." << std::endl;
    
    // 初始化 TimeSync 服务
    if (config_.enable_time_sync) {
        time_sync_service_ = std::make_unique<TimeSyncService>(ipc_bridge_.get());
        if (!time_sync_service_->initialize()) {
            std::cerr << "Failed to initialize TimeSync service" << std::endl;
            return false;
        }
    }
    
    // 初始化 Location 服务
    if (config_.enable_location) {
        location_service_ = std::make_unique<LocationService>(ipc_bridge_.get());
        if (!location_service_->initialize()) {
            std::cerr << "Failed to initialize Location service" << std::endl;
            return false;
        }
    }
    
    // 初始化 NetStatus 服务
    if (config_.enable_net_status) {
        net_status_service_ = std::make_unique<NetStatusService>(ipc_bridge_.get());
        if (!net_status_service_->initialize()) {
            std::cerr << "Failed to initialize NetStatus service" << std::endl;
            return false;
        }
    }
    
    // 初始化 RemoteCmdRelay 服务
    if (config_.enable_remote_cmd_relay) {
        remote_cmd_relay_service_ = std::make_unique<RemoteCmdRelayService>(ipc_bridge_.get());
        if (!remote_cmd_relay_service_->initialize()) {
            std::cerr << "Failed to initialize RemoteCmdRelay service" << std::endl;
            return false;
        }
    }
    
    // 初始化 FotaRelay 服务
    if (config_.enable_fota_relay) {
        fota_relay_service_ = std::make_unique<FotaRelayService>(ipc_bridge_.get());
        if (!fota_relay_service_->initialize()) {
            std::cerr << "Failed to initialize FotaRelay service" << std::endl;
            return false;
        }
    }
    
    std::cout << "All services initialized" << std::endl;
    return true;
}

bool SomeipDaemon::startServiceDiscovery() {
    if (!service_discovery_) {
        return false;
    }
    
    std::cout << "Starting service discovery..." << std::endl;
    
    // 统一 offer TBOX-SOMEIP 聚合服务
    if (!service_discovery_->offerService(
        someip_addressing::SERVICE_ID,
        someip_addressing::INSTANCE_ID))
    {
        std::cerr << "Failed to offer TBOX-SOMEIP service" << std::endl;
        return false;
    }
    
    // 注册 FOTA Software Inventory 上报处理器
    service_discovery_->registerMethodHandler(
        method_ids::REPORT_SOFTWARE_INVENTORY,
        [this](const SomeIpMessage& request) -> SomeIpMessage {
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
                std::cerr << "[FOTA] FotaRelayService not available" << std::endl;
                response.header.return_code = SomeIpReturnCode::E_NOT_OK;
                return response;
            }
            
            // 解析 payload
            VehicleSoftwareSnapshot snapshot;
            try {
                snapshot = parseSoftwareInventoryPayload(request.payload);
            } catch (const std::exception& e) {
                std::cerr << "[FOTA] Failed to parse payload: " << e.what() << std::endl;
                response.header.return_code = SomeIpReturnCode::E_NOT_OK;
                return response;
            }
            
            // 调用 FotaRelayService 上报
            ErrorCode result = fota_relay_service_->reportSoftwareInventory(snapshot);
            
            if (result == ErrorCode::SUCCESS) {
                response.header.return_code = SomeIpReturnCode::E_OK;
                std::cout << "[FOTA] Software inventory reported successfully for VIN: " 
                          << snapshot.vin << std::endl;
            } else {
                response.header.return_code = SomeIpReturnCode::E_NOT_OK;
                std::cerr << "[FOTA] Failed to report software inventory, error code: " 
                          << static_cast<uint16_t>(result) << std::endl;
            }
            
            return response;
        });
    
    // 订阅客户端连接事件
    service_discovery_->subscribeClientConnection(
        someip_addressing::SERVICE_ID,
        someip_addressing::INSTANCE_ID,
        [this](const ClientConnectionEvent& event) {
            handleClientConnection(event);
        });
    
    std::cout << "Service discovery started" << std::endl;
    return true;
}

void SomeipDaemon::handleClientConnection(const ClientConnectionEvent& event) {
    if (event.connected) {
        std::cout << "[CLIENT_CONNECTED] Client 0x" << std::hex << event.client_id
                  << " connected to service 0x" << event.service_id
                  << " instance 0x" << event.instance_id << std::endl;
        
        // 如果是 FOTA 相关的服务，打印特殊日志
        if (event.service_id == someip_addressing::SERVICE_ID && 
            event.instance_id == someip_addressing::INSTANCE_ID) {
            std::cout << "[FOTA_CLIENT] CGW-FOTA client connected via SOME/IP" << std::endl;
        }
    } else {
        std::cout << "[CLIENT_DISCONNECTED] Client 0x" << std::hex << event.client_id
                  << " disconnected from service 0x" << event.service_id
                  << " instance 0x" << event.instance_id << std::endl;
        
        if (event.service_id == someip_addressing::SERVICE_ID && 
            event.instance_id == someip_addressing::INSTANCE_ID) {
            std::cout << "[FOTA_CLIENT] CGW-FOTA client disconnected" << std::endl;
        }
    }
}



} // namespace someip
} // namespace tbox
