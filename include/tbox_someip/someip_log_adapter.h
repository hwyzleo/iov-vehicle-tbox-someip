#pragma once

#include "log.h"
#include "log_types.h"
#include <string>

namespace tbox {
namespace someip {

/**
 * TBOX-SOMEIP 日志适配器
 * 封装 framework-log 的初始化和模块 Logger 获取
 *
 * 模块划分（对应 TBOX-SOMEIP-DSN-CR-004 / CR-008）：
 * - runtime:    vsomeip/CommonAPI 初始化、启动、停止与异常
 * - sd:         Offer/StopOffer、订阅和服务发现状态
 * - method:     Method 入站、响应、拒绝与超时
 * - event:      Event/Notification 生成、下发与订阅者状态
 * - ipc_bridge: 内部 client 调用、回调和 SOME/IP↔IPC 映射
 * - serializer: 序列化、反序列化和版本兼容错误
 * - vehicle_message: 通用车云消息上下行中继结果（CR-008，取代旧 fota_relay）
 */
class SomeipLogAdapter {
public:
    // 初始化日志系统（在 main.cpp 中调用一次）
    static tbox::fw::log::InitResult init(
        const std::string& service,
        const tbox::fw::log::LogConfig& config
    );

    // 获取各模块的 Logger 实例
    static tbox::fw::log::Logger runtime();
    static tbox::fw::log::Logger sd();
    static tbox::fw::log::Logger method();
    static tbox::fw::log::Logger event();
    static tbox::fw::log::Logger ipc_bridge();
    static tbox::fw::log::Logger serializer();
    static tbox::fw::log::Logger vehicle_message();

    // 检查日志系统是否已初始化
    static bool isInitialized() { return s_initialized; }

private:
    static bool s_initialized;
};

} // namespace someip
} // namespace tbox
