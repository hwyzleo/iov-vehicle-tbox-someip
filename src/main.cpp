#include "someip_daemon.h"
#include "tbox_someip/someip_log_adapter.h"
#include "tbox_someip/someip_context.h"
#include "config.h"

#include <iostream>
#include <csignal>
#include <atomic>
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

    // 5. 从配置读取服务参数
    tbox::someip::ServiceConfig config;
    config.enable_net_status = cfg->getBool("someip.enable_net_status", true);
    config.enable_remote_cmd_relay = cfg->getBool("someip.enable_remote_cmd_relay", true);
    config.enable_fota_relay = cfg->getBool("someip.enable_fota_relay", true);

    // IPC 桥接配置 (CR-005/CR-006)
    config.tsp_ipc_socket_path = cfg->getString("someip.ipc_bridge.tsp_socket_path", "/tmp/tbox-tsp.sock");
    config.prov_ipc_socket_path = cfg->getString("someip.ipc_bridge.prov_socket_path", "/tmp/tbox-prov.sock");
    config.ipc_default_timeout_ms = static_cast<uint32_t>(cfg->getInt("someip.ipc_bridge.default_timeout_ms", 3000));
    config.ipc_event_queue_size = static_cast<uint32_t>(cfg->getInt("someip.ipc_bridge.event_queue_size", 256));

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
