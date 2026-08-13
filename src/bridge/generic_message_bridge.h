#pragma once

// GenericMessageBridge（CR-008）：TBOX-SOMEIP 聚合 Service 内通用车云消息桥接。
// 只消费公共 Envelope，payload 透明；上行经 ITspClient 通用 exchange 转发 TSP，
// 下行经独立订阅 + 有界队列 + 单源有序 worker 推送 onVehicleMessage。
// TSP 通用能力未就绪/版本不兼容时进入 DEGRADED，对外返回 Unavailable /
// VersionMismatch，绝不回退旧 FOTA 路径（US-013）。

#include "tbox_someip/interfaces.h"
#include "tbox_someip/types.h"
#include "tbox_someip/vehicle_message.h"
#include "vehicle_message_validator.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <queue>
#include <thread>

namespace tbox {
namespace someip {

class GenericMessageBridge {
public:
    GenericMessageBridge();
    ~GenericMessageBridge();

    GenericMessageBridge(const GenericMessageBridge&) = delete;
    GenericMessageBridge& operator=(const GenericMessageBridge&) = delete;

    // tsp 生命周期由调用方（gateway）持有；此处仅借用指针
    bool initialize(ITspClient* tsp, const GenericMessageConfig& cfg);
    void start();   // 确认 TSP 通用能力 + 建立下行订阅 + 启动 worker
    void stop();    // 停 worker、清队列、清 handler、复位 in-flight

    bool isDegraded() const;   // TSP 通用能力未就绪（DEGRADED）
    bool isReady() const;      // 已启动且非 DEGRADED

    // 上行：通用 Method 入口（校验 → in-flight 预算 → TSP exchange → 关联校验）
    VehicleMessageResponseFrame exchange(const VehicleMessageFrame& frame);

    // 下行：worker 单源有序调用；由 daemon 接到 notifyEvent
    void setDownlinkHandler(VehicleMessageHandler handler);

    VehicleMessageValidator& validator() { return validator_; }
    const GenericMessageConfig& config() const { return config_; }

private:
    void downlinkWorkerLoop();
    void onTspDownlink(const VehicleMessageFrame& frame);  // TSP callback（不阻塞）

    ITspClient* tsp_ = nullptr;
    GenericMessageConfig config_;

    VehicleMessageValidator validator_;
    VehicleMessageHandler downlink_handler_;
    mutable std::mutex handler_mutex_;

    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> degraded_{true};   // TSP 通用能力确认前默认 DEGRADED

    // 下行队列 + worker（独立有界队列，单源保序，不阻塞 TSP callback）
    std::queue<VehicleMessageFrame> downlink_queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::thread worker_thread_;

    // in-flight 预算（同步调用的并发上限）
    std::atomic<uint32_t> in_flight_{0};
};

} // namespace someip
} // namespace tbox
