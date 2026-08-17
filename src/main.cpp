#include "someip_daemon.h"
#include "tbox_someip/someip_log_adapter.h"
#include "tbox_someip/someip_context.h"
#include "config.h"

#include <iostream>
#include <csignal>
#include <atomic>
#include <string>
#include <unistd.h>

std::atomic<bool> g_shutdown_requested(false);

void signal_handler(int signal) {
    // 信号处理使用 stderr，因为 Logger 可能正在关闭
    std::cerr << "\n[signal] received signal " << signal << ", requesting shutdown" << std::endl;
    g_shutdown_requested = true;
}

int main(int argc, char* argv[]) {
    // 设置信号处理
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGPIPE, SIG_IGN);

    // ============================================================
    // 启动顺序（TBOX-SOMEIP-DSN-CR-004）：
    // Config::load("someip") -> 读取 common.log.* / someip.log.*
    // -> Logger::init("someip", logConfig) -> 创建模块 Logger
    // -> 初始化内部 client 桩与 SOME/IP runtime -> OfferService
    // ============================================================

    // 1. 加载框架配置
    auto config_err = CONFIG_MANAGER.load("someip");
    if (config_err != hwyz::config::ConfigError::kOk) {
        auto info = CONFIG_MANAGER.getLastError();
        std::cerr << "FATAL: Config load failed: " << info.message << std::endl;
        return 1;
    }

    auto cfg = CONFIG_SNAPSHOT;

    // 2. 读取日志配置（common.log.* + someip.log.*）
    tbox::fw::log::LogConfig log_config;
    log_config.level = tbox::fw::log::LogLevel::kInfo;
    log_config.strict = true;  // fail-closed: Logger 初始化失败则退出

    // 从配置读取日志级别
    std::string log_level_str = cfg->getString("common.log.level", "info");
    // someip.log.level 可覆盖 common.log.level
    log_level_str = cfg->getString("someip.log.level", log_level_str);

    if (log_level_str == "trace") log_config.level = tbox::fw::log::LogLevel::kTrace;
    else if (log_level_str == "debug") log_config.level = tbox::fw::log::LogLevel::kDebug;
    else if (log_level_str == "info") log_config.level = tbox::fw::log::LogLevel::kInfo;
    else if (log_level_str == "warn") log_config.level = tbox::fw::log::LogLevel::kWarn;
    else if (log_level_str == "error") log_config.level = tbox::fw::log::LogLevel::kError;

    // 读取 strict 模式
    log_config.strict = cfg->getBool("common.log.strict", true);

    // 读取异步队列配置
    log_config.async_config.enabled = cfg->getBool("common.log.async.enabled", true);
    log_config.async_config.queue_size = static_cast<uint32_t>(cfg->getInt("common.log.async.queue_size", 4096));
    log_config.async_config.flush_interval_ms = static_cast<uint32_t>(cfg->getInt("common.log.async.flush_interval_ms", 1000));

    // 读取 console sink 配置
    log_config.console_config.enabled = cfg->getBool("common.log.console.enabled", true);

    // 读取脱敏配置
    log_config.redact_config.identifiers = cfg->getString("common.log.redact.identifiers", "mask");
    log_config.redact_config.raw_payload_max_bytes = static_cast<uint32_t>(cfg->getInt("common.log.redact.raw_payload_max_bytes", 256));

    // 读取模块级别覆盖（someip.log.modules.<module>）
    auto modules_section = cfg->getSection("someip.log.modules");
    if (modules_section) {
        for (const auto& key : modules_section->getKeys()) {
            std::string level_str = modules_section->getString(key, "");
            if (level_str == "trace") log_config.module_levels[key] = tbox::fw::log::LogLevel::kTrace;
            else if (level_str == "debug") log_config.module_levels[key] = tbox::fw::log::LogLevel::kDebug;
            else if (level_str == "info") log_config.module_levels[key] = tbox::fw::log::LogLevel::kInfo;
            else if (level_str == "warn") log_config.module_levels[key] = tbox::fw::log::LogLevel::kWarn;
            else if (level_str == "error") log_config.module_levels[key] = tbox::fw::log::LogLevel::kError;
        }
    }

    // 3. 初始化 Logger（严格模式 fail-closed）
    auto log_result = tbox::someip::SomeipLogAdapter::init("someip", log_config);
    if (log_result.error != tbox::fw::log::LogError::kOk) {
        std::cerr << "FATAL: Logger init failed: " << log_result.error_message << std::endl;
        return 1;
    }

    // 4. 记录初始化成功
    tbox::someip::SomeipLogAdapter::runtime().info("someip.runtime.initialized",
        "Logger initialized successfully",
        {tbox::fw::log::Field("service", tbox::fw::log::FieldValue::makeString("someip")),
         tbox::fw::log::Field("log_level", tbox::fw::log::FieldValue::makeString(log_level_str)),
         tbox::fw::log::Field("strict", tbox::fw::log::FieldValue::makeBool(log_config.strict))}
    );

    tbox::someip::SomeipLogAdapter::runtime().info("someip.runtime.starting",
        "TBOX-SOMEIP Service Starting"
    );

    // 5. 从配置读取服务参数（CR-007 方案 C：config 为运行时 SSOT；缺失字段 fallback 到 types.h 基线默认值）
    tbox::someip::ServiceConfig config;

    // 寻址
    config.service_id = static_cast<uint16_t>(cfg->getInt("someip.service.service_id", config.service_id));
    config.instance_id = static_cast<uint16_t>(cfg->getInt("someip.service.instance_id", config.instance_id));
    config.transport = cfg->getString("someip.service.transport", config.transport);
    config.port = static_cast<uint16_t>(cfg->getInt("someip.service.port", config.port));

    // 服务发现
    config.sd_protocol = cfg->getString("someip.service_discovery.protocol", config.sd_protocol);
    config.sd_port = static_cast<uint16_t>(cfg->getInt("someip.service_discovery.port", config.sd_port));
    config.sd_initial_delay_ms = static_cast<uint32_t>(cfg->getInt("someip.service_discovery.initial_delay_ms", config.sd_initial_delay_ms));
    config.sd_repetition_base_delay_ms = static_cast<uint32_t>(cfg->getInt("someip.service_discovery.repetition_base_delay_ms", config.sd_repetition_base_delay_ms));

    // 通用车云消息成员（CR-008）：精确 ID 由 Registry 分配（US-011）；
    // someip.yaml 未配置（<registry-assigned> 待回填）时不注册，保持 nullopt。
    if (cfg->has("someip.members.vehicle_message_exchange.id")) {
        config.method_exchange_vehicle_message_id = static_cast<uint16_t>(
            cfg->getInt("someip.members.vehicle_message_exchange.id", 0));
    }
    if (cfg->has("someip.members.vehicle_message_downlink.id")) {
        config.event_vehicle_message_id = static_cast<uint16_t>(
            cfg->getInt("someip.members.vehicle_message_downlink.id", 0));
    }
    config.eventgroup_id = static_cast<uint32_t>(
        cfg->getInt("someip.members.vehicle_message_downlink.eventgroup_id", config.eventgroup_id));
    config.interface_major = static_cast<uint32_t>(
        cfg->getInt("someip.members.vehicle_message_exchange.interface_major", config.interface_major));

    // 通用车云消息限制（generic_message.*）
    auto allowed_services = cfg->getStringList("someip.generic_message.allowed_services");
    if (!allowed_services.empty()) {
        config.generic_message.limits.allowed_services = std::move(allowed_services);
    }
    auto allowed_versions = cfg->getStringList("someip.generic_message.allowed_protocol_versions");
    if (!allowed_versions.empty()) {
        config.generic_message.limits.allowed_protocol_versions = std::move(allowed_versions);
    }
    config.generic_message.limits.max_envelope_bytes = static_cast<uint32_t>(
        cfg->getInt("someip.generic_message.max_envelope_bytes", config.generic_message.limits.max_envelope_bytes));
    config.generic_message.limits.max_payload_bytes = static_cast<uint32_t>(
        cfg->getInt("someip.generic_message.max_payload_bytes", config.generic_message.limits.max_payload_bytes));
    config.generic_message.max_in_flight = static_cast<uint32_t>(
        cfg->getInt("someip.generic_message.max_in_flight", config.generic_message.max_in_flight));
    config.generic_message.downlink_queue_capacity = static_cast<uint32_t>(
        cfg->getInt("someip.generic_message.downlink_queue_capacity", config.generic_message.downlink_queue_capacity));
    config.generic_message.worker_count = static_cast<uint32_t>(
        cfg->getInt("someip.generic_message.worker_count", config.generic_message.worker_count));
    config.generic_message.tsp_timeout_ms = static_cast<uint32_t>(
        cfg->getInt("someip.generic_message.tsp_timeout_ms", config.generic_message.tsp_timeout_ms));

    // 桥接（CR-005/CR-006/CR-007/CR-008）
    config.bridge_request_deadline_ms = static_cast<uint32_t>(cfg->getInt("someip.bridge.request_deadline_ms", config.bridge_request_deadline_ms));
    config.bridge_queue_capacity = static_cast<uint32_t>(cfg->getInt("someip.bridge.queue_capacity", config.bridge_queue_capacity));
    config.bridge_worker_count = static_cast<uint32_t>(cfg->getInt("someip.bridge.worker_count", config.bridge_worker_count));

    // 严格校验 tsp_timeout_ms < request_deadline_ms（CR-008 §配置增量）
    if (config.generic_message.tsp_timeout_ms >= config.bridge_request_deadline_ms) {
        tbox::someip::SomeipLogAdapter::runtime().error("someip.runtime.config_invalid",
            "someip.generic_message.tsp_timeout_ms must be < someip.bridge.request_deadline_ms",
            {tbox::fw::log::Field("tsp_timeout_ms", tbox::fw::log::FieldValue::makeInt(
                 config.generic_message.tsp_timeout_ms)),
             tbox::fw::log::Field("request_deadline_ms", tbox::fw::log::FieldValue::makeInt(
                 config.bridge_request_deadline_ms))}
        );
        return 1;
    }

    // 内部 client facade 连接
    config.tsp.socket_path = cfg->getString("someip.clients.tsp.socket_path", config.tsp.socket_path);
    config.tsp.connect_timeout_ms = static_cast<uint32_t>(cfg->getInt("someip.clients.tsp.connect_timeout_ms", config.tsp.connect_timeout_ms));
    config.prov.socket_path = cfg->getString("someip.clients.prov.socket_path", config.prov.socket_path);
    config.prov.connect_timeout_ms = static_cast<uint32_t>(cfg->getInt("someip.clients.prov.connect_timeout_ms", config.prov.connect_timeout_ms));

    // 服务开关（默认 true；仅 dev profile 配置，production schema 拒绝 CR-007 §8 REQ-008）
    config.enable_net_status = cfg->getBool("someip.enable_net_status", true);
    config.enable_remote_cmd_relay = cfg->getBool("someip.enable_remote_cmd_relay", true);
    config.enable_vehicle_message_gateway = cfg->getBool("someip.enable_vehicle_message_gateway", true);

    // 6. 创建并初始化守护进程
    tbox::someip::SomeipDaemon daemon;

    if (!daemon.initialize(config)) {
        tbox::someip::SomeipLogAdapter::runtime().error("someip.runtime.init_failed",
            "Failed to initialize TBOX-SOMEIP daemon"
        );
        return 1;
    }

    // 7. 启动守护进程（含 OfferService）
    if (!daemon.start()) {
        tbox::someip::SomeipLogAdapter::runtime().error("someip.runtime.start_failed",
            "Failed to start TBOX-SOMEIP daemon"
        );
        return 1;
    }

    tbox::someip::SomeipLogAdapter::runtime().info("someip.runtime.started",
        "TBOX-SOMEIP daemon started successfully"
    );

    // 主循环
    while (!g_shutdown_requested && daemon.isRunning()) {
        sleep(1);
    }

    tbox::someip::SomeipLogAdapter::runtime().info("someip.runtime.shutting_down",
        "Shutting down TBOX-SOMEIP daemon..."
    );

    daemon.shutdown();

    tbox::someip::SomeipLogAdapter::runtime().info("someip.runtime.stopped",
        "TBOX-SOMEIP daemon stopped"
    );

    return 0;
}
