#include "tbox_someip/someip_service.h"
#include "tbox_someip/someip_log_adapter.h"
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <thread>
#include <vector>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <poll.h>
#include <cstring>

namespace {

inline void writeU16BE(uint8_t* p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

inline void writeU32BE(uint8_t* p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<uint8_t>(v & 0xFF);
}

inline uint16_t readU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | p[1]);
}

inline uint32_t readU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) |
           (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8)  |
           static_cast<uint32_t>(p[3]);
}

inline void serializeHeader(uint8_t buf[16], const tbox::someip::SomeIpHeader& h) {
    writeU16BE(buf + 0, h.service_id);
    writeU16BE(buf + 2, h.method_id);
    writeU32BE(buf + 4, h.length);
    writeU16BE(buf + 8, h.client_id);
    writeU16BE(buf + 10, h.session_id);
    buf[12] = h.protocol_version;
    buf[13] = h.interface_version;
    buf[14] = static_cast<uint8_t>(h.message_type);
    buf[15] = static_cast<uint8_t>(h.return_code);
}

inline bool deserializeHeader(const uint8_t buf[16], tbox::someip::SomeIpHeader& h) {
    h.service_id = readU16BE(buf + 0);
    h.method_id = readU16BE(buf + 2);
    h.length = readU32BE(buf + 4);
    h.client_id = readU16BE(buf + 8);
    h.session_id = readU16BE(buf + 10);
    h.protocol_version = buf[12];
    h.interface_version = buf[13];
    h.message_type = static_cast<tbox::someip::SomeIpMessageType>(buf[14]);
    h.return_code = static_cast<tbox::someip::SomeIpReturnCode>(buf[15]);
    return true;
}

inline bool readExact(int fd, void* buf, size_t n, int timeoutMs) {
    size_t left = n;
    auto* p = static_cast<uint8_t*>(buf);
    while (left > 0) {
        pollfd pfd{fd, POLLIN, 0};
        int pr = ::poll(&pfd, 1, timeoutMs);
        if (pr == 0) return false; // timeout
        if (pr < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        ssize_t r = ::recv(fd, p, left, 0);
        if (r <= 0) return false;
        p += r;
        left -= static_cast<size_t>(r);
    }
    return true;
}

inline bool writeAll(int fd, const void* buf, size_t n) {
    const auto* p = static_cast<const uint8_t*>(buf);
    size_t left = n;
    while (left > 0) {
        ssize_t w = ::send(fd, p, left, 0);
        if (w <= 0) {
            if (errno == EINTR) continue;
            return false;
        }
        p += w;
        left -= static_cast<size_t>(w);
    }
    return true;
}

} // namespace

namespace tbox {
namespace someip {

#ifdef USE_MOCK_SOMEIP
// Mock 实现
class SomeIpServiceMock : public SomeIpService {
public:
    SomeIpServiceMock() = default;
    ~SomeIpServiceMock() override { stop(); }

    bool initialize(const SomeIpServiceConfig& config) override {
        if (running_) {
            return false;
        }
        config_ = config;

        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            SomeipLogAdapter::runtime().error("someip.runtime.socket_failed",
                "MockSomeIP socket() failed",
                {tbox::fw::log::Field("error", tbox::fw::log::FieldValue::makeString(std::strerror(errno))),
                 tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1001"))}
            );
            return false;
        }

        int opt = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(config_.port);
        if (::inet_pton(AF_INET, config_.ip_address.c_str(), &addr.sin_addr) != 1) {
            SomeipLogAdapter::runtime().error("someip.runtime.inet_pton_failed",
                "MockSomeIP inet_pton() failed",
                {tbox::fw::log::Field("ip_address", tbox::fw::log::FieldValue::makeString(config_.ip_address)),
                 tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1001"))}
            );
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        if (::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
            SomeipLogAdapter::runtime().error("someip.runtime.bind_failed",
                "MockSomeIP bind() failed",
                {tbox::fw::log::Field("error", tbox::fw::log::FieldValue::makeString(std::strerror(errno))),
                 tbox::fw::log::Field("port", tbox::fw::log::FieldValue::makeInt(config_.port)),
                 tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1001"))}
            );
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        if (::listen(listen_fd_, 1) < 0) {
            SomeipLogAdapter::runtime().error("someip.runtime.listen_failed",
                "MockSomeIP listen() failed",
                {tbox::fw::log::Field("error", tbox::fw::log::FieldValue::makeString(std::strerror(errno))),
                 tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1001"))}
            );
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }

        // 获取实际绑定的端口（当使用端口 0 时）
        sockaddr_in actual_addr{};
        socklen_t actual_len = sizeof(actual_addr);
        if (::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&actual_addr), &actual_len) == 0) {
            bound_port_ = ntohs(actual_addr.sin_port);
        } else {
            bound_port_ = config_.port;
        }

        bound_address_ = config_.ip_address;
        initialized_ = true;
        return true;
    }

    bool start() override {
        if (!initialized_ || running_) {
            return false;
        }
        stop_requested_ = false;
        running_ = true;

        listener_thread_ = std::thread([this]() {
            SomeipLogAdapter::sd().info("someip.sd.listening",
                "MockSomeIP listening for connections",
                {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(config_.service_id)),
                 tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(config_.instance_id)),
                 tbox::fw::log::Field("bind_address", tbox::fw::log::FieldValue::makeString(bound_address_)),
                 tbox::fw::log::Field("port", tbox::fw::log::FieldValue::makeInt(bound_port_))}
            );
            while (!stop_requested_) {
                pollfd pfd{listen_fd_, POLLIN, 0};
                int pr = ::poll(&pfd, 1, 200); // 200ms poll interval
                if (pr == 0) continue;
                if (pr < 0) {
                    if (errno == EINTR) continue;
                    break;
                }

                sockaddr_in cli{};
                socklen_t cliLen = sizeof(cli);
                int fd = ::accept(listen_fd_, reinterpret_cast<sockaddr*>(&cli), &cliLen);
                if (fd < 0) {
                    if (errno == EINTR) continue;
                    SomeipLogAdapter::sd().warn("someip.sd.accept_failed",
                        "MockSomeIP accept() failed",
                        {tbox::fw::log::Field("error", tbox::fw::log::FieldValue::makeString(std::strerror(errno)))}
                    );
                    continue;
                }

                // Single connection model
                if (client_fd_ >= 0) {
                    ::close(fd);
                    continue;
                }
                client_fd_ = fd;
                {
                    char addr_buf[INET_ADDRSTRLEN] = {0};
                    ::inet_ntop(AF_INET, &cli.sin_addr, addr_buf, sizeof(addr_buf));
                    SomeipLogAdapter::sd().info("someip.sd.subscription.changed",
                        "MockSomeIP client connected",
                        {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(config_.service_id)),
                         tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(config_.instance_id)),
                         tbox::fw::log::Field("remote_endpoint", tbox::fw::log::FieldValue::makeString(std::string(addr_buf) + ":" + std::to_string(ntohs(cli.sin_port)))),
                         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("connected"))}
                    );
                }

                // Simple synchronous request processing loop
                while (!stop_requested_) {
                    uint8_t hdrBuf[16];
                    if (!readExact(client_fd_, hdrBuf, 16, 2000)) {
                        break; // timeout or error
                    }

                    SomeIpHeader hdr{};
                    deserializeHeader(hdrBuf, hdr);

                    // Calculate payload length from length field
                    // length = 8 + payload_size, so payload_size = length - 8
                    uint32_t payload_length = (hdr.length > 8) ? (hdr.length - 8) : 0;
                    std::vector<uint8_t> payload;
                    if (payload_length > 0) {
                        if (payload_length > 10 * 1024 * 1024) { // 10MB sanity limit
                            break;
                        }
                        payload.resize(payload_length);
                        if (!readExact(client_fd_, payload.data(), payload_length, 5000)) {
                            break;
                        }
                    }

                    SomeipLogAdapter::method().debug("someip.method.request.received",
                        "MockSomeIP request received",
                        {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(hdr.service_id)),
                         tbox::fw::log::Field("someip_method_id", tbox::fw::log::FieldValue::makeInt(hdr.method_id)),
                         tbox::fw::log::Field("someip_client_id", tbox::fw::log::FieldValue::makeInt(hdr.client_id)),
                         tbox::fw::log::Field("payload_length", tbox::fw::log::FieldValue::makeInt(static_cast<int64_t>(payload_length)))}
                    );

                    // Map to SomeIpMessage
                    SomeIpMessage request;
                    request.header.service_id = hdr.service_id;
                    request.header.method_id = hdr.method_id;
                    request.header.client_id = hdr.client_id;
                    request.header.session_id = hdr.session_id;
                    request.header.message_type = static_cast<SomeIpMessageType>(hdr.message_type);
                    request.payload = std::move(payload);

                    // Find handler
                    SomeIpRequestHandler handler;
                    {
                        std::lock_guard<std::mutex> lock(handlers_mutex_);
                        auto it = method_handlers_.find(request.header.method_id);
                        if (it != method_handlers_.end()) {
                            handler = it->second;
                        }
                    }

                    SomeIpMessage response;
                    if (handler) {
                        response = handler(request);
                    } else {
                        response.header.service_id = request.header.service_id;
                        response.header.method_id = request.header.method_id;
                        response.header.message_type = SomeIpMessageType::RESPONSE;
                        response.header.return_code = SomeIpReturnCode::E_NOT_OK;
                    }

                    // Serialize response frame
                    SomeIpHeader respHdr{};
                    respHdr.service_id = response.header.service_id;
                    respHdr.method_id = response.header.method_id;
                    // length = 8 bytes (client_id + session_id + protocol/interface/message_type/return_code) + payload size
                    respHdr.length = static_cast<uint32_t>(8 + response.payload.size());
                    respHdr.client_id = response.header.client_id;
                    respHdr.session_id = hdr.session_id; // echo session
                    respHdr.protocol_version = 0x01;
                    respHdr.interface_version = 0x01;
                    respHdr.message_type = response.header.message_type;
                    respHdr.return_code = response.header.return_code;

                    uint8_t respBuf[16];
                    serializeHeader(respBuf, respHdr);
                    if (!writeAll(client_fd_, respBuf, 16)) break;
                    if (!response.payload.empty()) {
                        if (!writeAll(client_fd_, response.payload.data(), response.payload.size())) break;
                    }
                }

                SomeipLogAdapter::sd().info("someip.sd.subscription.changed",
                    "MockSomeIP client disconnected",
                    {tbox::fw::log::Field("someip_service_id", tbox::fw::log::FieldValue::makeInt(config_.service_id)),
                     tbox::fw::log::Field("someip_instance_id", tbox::fw::log::FieldValue::makeInt(config_.instance_id)),
                     tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("disconnected"))}
                );
                ::close(client_fd_);
                client_fd_ = -1;
            }
        });

        return true;
    }

    void stop() override {
        stop_requested_ = true;
        running_ = false;
        offering_ = false;

        if (listener_thread_.joinable()) {
            listener_thread_.join();
        }
        if (client_fd_ >= 0) {
            ::close(client_fd_);
            client_fd_ = -1;
        }
        if (listen_fd_ >= 0) {
            ::close(listen_fd_);
            listen_fd_ = -1;
        }
    }

    bool isRunning() const override {
        return running_;
    }

    void registerMethodHandler(MethodId method_id, SomeIpRequestHandler handler) override {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        method_handlers_[method_id] = std::move(handler);
    }

    void registerEventHandler(EventId event_id, SomeIpNotificationHandler handler) override {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        event_handlers_[event_id] = std::move(handler);
    }

    bool offerService() override {
        if (!running_ || !initialized_) {
            return false;
        }
        offering_ = true;
        return true;
    }

    void stopOfferService() override {
        offering_ = false;
    }

    ServiceId getServiceId() const override {
        return config_.service_id;
    }

    uint16_t getInstanceId() const override {
        return config_.instance_id;
    }

    uint16_t getBoundPort() const override {
        return bound_port_;
    }

private:
    SomeIpServiceConfig config_;
    std::atomic<bool> initialized_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> offering_{false};

    std::mutex handlers_mutex_;
    std::unordered_map<MethodId, SomeIpRequestHandler> method_handlers_;
    std::unordered_map<EventId, SomeIpNotificationHandler> event_handlers_;

    // TCP transport members
    int listen_fd_{-1};
    int client_fd_{-1};
    std::thread listener_thread_;
    std::atomic<bool> stop_requested_{false};
    std::string bound_address_;
    uint16_t bound_port_{0};
};

std::unique_ptr<SomeIpService> createSomeIpService() {
    return std::make_unique<SomeIpServiceMock>();
}

#else
// vsomeip 实现（需要实际的 vsomeip 库）
// TODO: 实现真实的 vsomeip 集成
std::unique_ptr<SomeIpService> createSomeIpService() {
    // 临时返回 nullptr，需要实现真实的 vsomeip 集成
    return nullptr;
}
#endif

} // namespace someip
} // namespace tbox