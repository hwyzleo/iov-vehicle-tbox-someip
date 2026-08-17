#pragma once

// Mock TSP IPC peer（真实 framework-ipc Server + TSP 通用方法处理）。
// 仅测试使用：作为 TSP daemon 的业务对端，验证 SOMEIP 侧真实
// tbox::tsp_client 通用 API 的 IPC 往返（exchange/subscribe/downlink）。

#include "ipc.h"  // tbox::fw::ipc::Server

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace tbox {
namespace someip {
namespace test {

// TSP IPC 协议常量（mirror TSP 仓库 include/tsp_ipc_protocol.h；仅测试对端用）
constexpr uint32_t kMethodExchange = 2;              // EXCHANGE_VEHICLE_MESSAGE
constexpr uint32_t kMethodSubscribe = 3;             // SUBSCRIBE_VEHICLE_MESSAGE
constexpr uint32_t kEventVehicleMessage = 102;       // VEHICLE_MESSAGE (downlink)

std::string b64_encode_bytes(const std::vector<std::byte>& data);

// mock TSP IPC peer：真实 framework-ipc Server + TSP 通用方法处理
class MockTspPeer {
public:
    explicit MockTspPeer(std::string socket_path)
        : socket_path_(std::move(socket_path))
        , server_(socket_path_) {}

    ~MockTspPeer() { stop(); }

    void set_exchange_response(std::string outcome, int32_t status = 0,
                               std::string error = "",
                               std::vector<std::byte> response_envelope = {}) {
        exchange_outcome_ = std::move(outcome);
        exchange_status_ = status;
        exchange_error_ = std::move(error);
        exchange_response_ = std::move(response_envelope);
    }

    bool start() {
        auto* self = this;
        return server_.start(
            [self](uint32_t method_id, std::string_view params_json, int client_fd) -> std::string {
                return self->handle(method_id, params_json, client_fd);
            });
    }

    void stop() { server_.stop(); }

    // 推送下行 EVENT；无订阅者时返回 false（调用方可重试等待订阅就绪）
    bool push_downlink(const std::string& service,
                       const std::vector<std::byte>& envelope) {
        std::string payload =
            "{\"service\":\"" + json_escape(service) +
            "\",\"envelope_base64\":\"" + b64_encode_bytes(envelope) + "\"}";
        return server_.push_event(kEventVehicleMessage, payload);
    }

    int exchange_calls() const { return exchange_calls_.load(); }

private:
    std::string handle(uint32_t method_id, std::string_view params_json, int client_fd) {
        (void)params_json;
        switch (method_id) {
            case kMethodSubscribe:
                // 注册该连接的 EVENT 订阅后返回成功（status_code==0 → 握手通过）
                server_.add_subscription(client_fd, kEventVehicleMessage);
                return "{\"success\":true}";
            case kMethodExchange: {
                exchange_calls_.fetch_add(1);
                std::string b64 = exchange_response_.empty()
                    ? "" : b64_encode_bytes(exchange_response_);
                return "{\"outcome\":\"" + json_escape(exchange_outcome_) +
                       "\",\"status\":" + std::to_string(exchange_status_) +
                       ",\"error\":\"" + json_escape(exchange_error_) +
                       "\",\"envelope_base64\":\"" + b64 + "\"}";
            }
            default:
                return "{\"status\":306}";
        }
    }

    static std::string json_escape(const std::string& s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') out.push_back('\\');
            out.push_back(c);
        }
        return out;
    }

    std::string socket_path_;
    tbox::fw::ipc::Server server_;
    std::string exchange_outcome_ = "Accepted";
    int32_t exchange_status_ = 0;
    std::string exchange_error_;
    std::vector<std::byte> exchange_response_;
    std::atomic<int> exchange_calls_{0};
};

inline std::string b64_encode_bytes(const std::vector<std::byte>& data) {
    static const char* tbl =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 3 <= data.size()) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8) |
                     static_cast<uint32_t>(data[i + 2]);
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(tbl[(n >> 6) & 63]);
        out.push_back(tbl[n & 63]);
        i += 3;
    }
    if (i + 1 == data.size()) {
        uint32_t n = static_cast<uint32_t>(data[i]) << 16;
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back('=');
        out.push_back('=');
    } else if (i + 2 == data.size()) {
        uint32_t n = (static_cast<uint32_t>(data[i]) << 16) |
                     (static_cast<uint32_t>(data[i + 1]) << 8);
        out.push_back(tbl[(n >> 18) & 63]);
        out.push_back(tbl[(n >> 12) & 63]);
        out.push_back(tbl[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

// 生成唯一测试 socket 路径
inline std::string unique_test_socket(const char* tag) {
    static std::atomic<uint64_t> counter{0};
    return std::string("/tmp/tbox-someip-") + tag + "-" +
           std::to_string(::getpid()) + "-" +
           std::to_string(counter.fetch_add(1)) + ".sock";
}

} // namespace test
} // namespace someip
} // namespace tbox
