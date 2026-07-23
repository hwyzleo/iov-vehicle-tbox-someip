#pragma once

/**
 * MockSomeipStack - vsomeip 模拟实现
 * 
 * 用于开发和测试阶段，模拟 vsomeip 的核心 API。
 * 真正的 vsomeip 集成推迟到 CI/CD 或 Linux 环境。
 * 
 * 使用方法：
 * - 在 CMakeLists.txt 中定义 USE_MOCK_SOMEIP
 * - 代码中通过 #ifdef USE_MOCK_SOMEIP 选择使用模拟或真实实现
 */

#include <memory>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>
#include <mutex>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <algorithm>
#include <utility>

namespace mock_someip {

// 类型定义（对齐 vsomeip API）
using service_t = uint16_t;
using instance_t = uint16_t;
using method_t = uint16_t;
using event_t = uint16_t;
using session_t = uint16_t;
using client_id_t = uint16_t;

constexpr service_t ANY_SERVICE = 0xFFFF;
constexpr instance_t ANY_INSTANCE = 0xFFFF;
constexpr method_t ANY_METHOD = 0xFFFF;
constexpr event_t ANY_EVENT = 0xFFFF;

// 消息类型
enum class message_type_e : uint8_t {
    REQUEST = 0x00,
    REQUEST_NO_RETURN = 0x01,
    NOTIFICATION = 0x02,
    RESPONSE = 0x80,
    ERROR = 0x81
};

// 返回码
enum class return_code_e : uint8_t {
    E_OK = 0x00,
    E_NOT_OK = 0x01,
    E_UNKNOWN_SERVICE = 0x02,
    E_UNKNOWN_METHOD = 0x03,
    E_NOT_READY = 0x04,
    E_NOT_REACHABLE = 0x05,
    E_TIMEOUT = 0x06,
    E_WRONG_PROTOCOL_VERSION = 0x07,
    E_WRONG_MESSAGE_TYPE = 0x08
};

// 前向声明
class Payload;
class Message;
class Request;
class Response;
class Runtime;
class Application;

// Payload 类
class Payload {
public:
    Payload() = default;
    Payload(const std::vector<uint8_t>& data) : data_(data) {}
    Payload(const uint8_t* data, size_t length) : data_(data, data + length) {}
    
    const uint8_t* get_data() const { return data_.data(); }
    size_t get_length() const { return data_.size(); }
    void set_data(const std::vector<uint8_t>& data) { data_ = data; }
    void set_data(const uint8_t* data, size_t length) { 
        data_.assign(data, data + length); 
    }
    
private:
    std::vector<uint8_t> data_;
};

// Message 基类
class Message {
public:
    virtual ~Message() = default;
    
    virtual service_t get_service() const = 0;
    virtual void set_service(service_t service) = 0;
    
    virtual instance_t get_instance() const = 0;
    virtual void set_instance(instance_t instance) = 0;
    
    virtual method_t get_method() const = 0;
    virtual void set_method(method_t method) = 0;
    
    virtual session_t get_session() const = 0;
    virtual void set_session(session_t session) = 0;
    
    virtual client_id_t get_client() const = 0;
    virtual void set_client(client_id_t client) = 0;
    
    virtual message_type_e get_message_type() const = 0;
    virtual void set_message_type(message_type_e type) = 0;
    
    virtual return_code_e get_return_code() const = 0;
    virtual void set_return_code(return_code_e code) = 0;
    
    virtual std::shared_ptr<Payload> get_payload() const = 0;
    virtual void set_payload(std::shared_ptr<Payload> payload) = 0;
};

// Request 类
class Request : public Message {
public:
    Request() : service_(0), instance_(0), method_(0), session_(0), 
                client_(0), type_(message_type_e::REQUEST), code_(return_code_e::E_OK) {}
    
    service_t get_service() const override { return service_; }
    void set_service(service_t service) override { service_ = service; }
    
    instance_t get_instance() const override { return instance_; }
    void set_instance(instance_t instance) override { instance_ = instance; }
    
    method_t get_method() const override { return method_; }
    void set_method(method_t method) override { method_ = method; }
    
    session_t get_session() const override { return session_; }
    void set_session(session_t session) override { session_ = session; }
    
    client_id_t get_client() const override { return client_; }
    void set_client(client_id_t client) override { client_ = client; }
    
    message_type_e get_message_type() const override { return type_; }
    void set_message_type(message_type_e type) override { type_ = type; }
    
    return_code_e get_return_code() const override { return code_; }
    void set_return_code(return_code_e code) override { code_ = code; }
    
    std::shared_ptr<Payload> get_payload() const override { return payload_; }
    void set_payload(std::shared_ptr<Payload> payload) override { payload_ = payload; }
    
private:
    service_t service_;
    instance_t instance_;
    method_t method_;
    session_t session_;
    client_id_t client_;
    message_type_e type_;
    return_code_e code_;
    std::shared_ptr<Payload> payload_;
};

// Response 类
class Response : public Message {
public:
    Response() : service_(0), instance_(0), method_(0), session_(0), 
                 client_(0), type_(message_type_e::RESPONSE), code_(return_code_e::E_OK) {}
    
    // 从 Request 创建 Response
    static std::shared_ptr<Response> create(const std::shared_ptr<Request>& request) {
        auto response = std::make_shared<Response>();
        response->set_service(request->get_service());
        response->set_instance(request->get_instance());
        response->set_method(request->get_method());
        response->set_session(request->get_session());
        response->set_client(request->get_client());
        response->set_message_type(message_type_e::RESPONSE);
        return response;
    }
    
    service_t get_service() const override { return service_; }
    void set_service(service_t service) override { service_ = service; }
    
    instance_t get_instance() const override { return instance_; }
    void set_instance(instance_t instance) override { instance_ = instance; }
    
    method_t get_method() const override { return method_; }
    void set_method(method_t method) override { method_ = method; }
    
    session_t get_session() const override { return session_; }
    void set_session(session_t session) override { session_ = session; }
    
    client_id_t get_client() const override { return client_; }
    void set_client(client_id_t client) override { client_ = client; }
    
    message_type_e get_message_type() const override { return type_; }
    void set_message_type(message_type_e type) override { type_ = type; }
    
    return_code_e get_return_code() const override { return code_; }
    void set_return_code(return_code_e code) override { code_ = code; }
    
    std::shared_ptr<Payload> get_payload() const override { return payload_; }
    void set_payload(std::shared_ptr<Payload> payload) override { payload_ = payload; }
    
private:
    service_t service_;
    instance_t instance_;
    method_t method_;
    session_t session_;
    client_id_t client_;
    message_type_e type_;
    return_code_e code_;
    std::shared_ptr<Payload> payload_;
};

// 消息处理器类型
using message_handler_t = std::function<void(const std::shared_ptr<Message>&)>;
using availability_handler_t = std::function<void(service_t, instance_t, bool)>;

// 全局服务注册表
class ServiceRegistry {
public:
    static ServiceRegistry& get_instance() {
        static ServiceRegistry instance;
        return instance;
    }
    
    void register_service(service_t service, instance_t instance) {
        std::vector<availability_handler_t> handlers;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            services_[service].insert(instance);
            
            // 收集需要通知的处理器
            auto key = std::make_pair(service, instance);
            auto it = availability_handlers_.find(key);
            if (it != availability_handlers_.end()) {
                for (auto& entry : it->second) {
                    handlers.push_back(entry.handler);
                }
            }
        }
        
        // 在锁外通知所有等待的客户端
        for (auto& handler : handlers) {
            handler(service, instance, true);
        }
    }
    
    void unregister_service(service_t service, instance_t instance) {
        std::vector<availability_handler_t> handlers;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (services_.count(service)) {
                services_[service].erase(instance);
                if (services_[service].empty()) {
                    services_.erase(service);
                }
            }
            
            // 收集需要通知的处理器
            auto key = std::make_pair(service, instance);
            auto it = availability_handlers_.find(key);
            if (it != availability_handlers_.end()) {
                for (auto& entry : it->second) {
                    handlers.push_back(entry.handler);
                }
            }
        }
        
        // 在锁外通知所有等待的客户端
        for (auto& handler : handlers) {
            handler(service, instance, false);
        }
    }
    
    bool is_service_available(service_t service, instance_t instance) const {
        std::lock_guard<std::mutex> lock(mutex_);
        return is_service_available_locked(service, instance);
    }
    
    // owner 用于标识注册者（通常为 Application*），以便在其销毁时移除回调，
    // 避免全局注册表持有悬垂的 std::function 造成 use-after-free。
    void register_availability_handler(service_t service, instance_t instance,
                                       const void* owner,
                                       availability_handler_t handler) {
        bool should_notify = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto key = std::make_pair(service, instance);
            availability_handlers_[key].push_back({owner, handler});
            
            // 如果服务已经可用，立即通知
            // 注意：此处必须使用无锁版本，mutex_ 已被持有，
            // 再次调用 is_service_available() 会在非递归锁上自死锁。
            should_notify = is_service_available_locked(service, instance);
        }
        
        // 在锁外通知
        if (should_notify) {
            handler(service, instance, true);
        }
    }
    
    // 移除某个 owner 注册的全部可用性回调。Application 析构时必须调用，
    // 否则后续 register/unregister_service 会调用已销毁对象的回调。
    void remove_handlers(const void* owner) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = availability_handlers_.begin(); it != availability_handlers_.end();) {
            auto& vec = it->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                                     [owner](const HandlerEntry& e) { return e.owner == owner; }),
                      vec.end());
            if (vec.empty()) {
                it = availability_handlers_.erase(it);
            } else {
                ++it;
            }
        }
    }
    
    // ---- 消息路由支持 ----
    // 记录提供某 (service,instance) 的 Application（服务端），
    // 以及按 client_id 索引的所有 Application，用于请求/响应的跨应用投递。
    void register_offerer(service_t service, instance_t instance, Application* app) {
        std::lock_guard<std::mutex> lock(mutex_);
        offerers_[std::make_pair(service, instance)] = app;
    }
    
    void unregister_offerer(service_t service, instance_t instance, Application* app) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto key = std::make_pair(service, instance);
        auto it = offerers_.find(key);
        if (it != offerers_.end() && it->second == app) {
            offerers_.erase(it);
        }
    }
    
    Application* find_offerer(service_t service, instance_t instance) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = offerers_.find(std::make_pair(service, instance));
        return it != offerers_.end() ? it->second : nullptr;
    }
    
    void register_application(client_id_t id, Application* app) {
        std::lock_guard<std::mutex> lock(mutex_);
        applications_[id] = app;
    }
    
    void unregister_application(client_id_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        applications_.erase(id);
    }
    
    Application* find_application(client_id_t id) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = applications_.find(id);
        return it != applications_.end() ? it->second : nullptr;
    }
    
private:
    ServiceRegistry() = default;
    
    // 无锁版本：调用者必须已持有 mutex_
    bool is_service_available_locked(service_t service, instance_t instance) const {
        auto it = services_.find(service);
        if (it != services_.end()) {
            return it->second.count(instance) > 0;
        }
        return false;
    }
    
    struct HandlerEntry {
        const void* owner;
        availability_handler_t handler;
    };
    
    mutable std::mutex mutex_;
    std::map<service_t, std::set<instance_t>> services_;
    std::map<std::pair<service_t, instance_t>, std::vector<HandlerEntry>> availability_handlers_;
    std::map<std::pair<service_t, instance_t>, Application*> offerers_;
    std::map<client_id_t, Application*> applications_;
};

// Runtime 类（单例）
class Runtime {
public:
    static std::shared_ptr<Runtime> get() {
        static std::shared_ptr<Runtime> instance = std::shared_ptr<Runtime>(new Runtime());
        return instance;
    }
    
    std::shared_ptr<Request> create_request() {
        return std::make_shared<Request>();
    }
    
    std::shared_ptr<Message> create_response(const std::shared_ptr<Message>& request) {
        // 从 Message 创建 Response
        auto response = std::make_shared<Response>();
        response->set_service(request->get_service());
        response->set_instance(request->get_instance());
        response->set_method(request->get_method());
        response->set_session(request->get_session());
        response->set_client(request->get_client());
        response->set_message_type(message_type_e::RESPONSE);
        return response;
    }
    
    std::shared_ptr<Payload> create_payload() {
        return std::make_shared<Payload>();
    }
    
    std::shared_ptr<Application> create_application(const std::string& name);
    
private:
    Runtime() = default;
};

// Application 类
class Application : public std::enable_shared_from_this<Application> {
public:
    Application(const std::string& name) : name_(name), running_(false) {
        static std::atomic<client_id_t> next_id{1};
        client_id_ = next_id.fetch_add(1);
        ServiceRegistry::get_instance().register_application(client_id_, this);
    }
    
    ~Application() {
        stop();
        ServiceRegistry::get_instance().unregister_application(client_id_);
    }
    
    void init() {
        // 初始化应用
    }
    
    bool start() {
        if (running_) return true;
        running_ = true;
        
        // 启动事件循环线程
        event_loop_thread_ = std::thread([this]() {
            while (running_) {
                std::shared_ptr<Message> msg;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex_);
                    cv_.wait_for(lock, std::chrono::milliseconds(100), 
                                [this]() { return !message_queue_.empty() || !running_; });
                    
                    if (!running_) break;
                    if (message_queue_.empty()) continue;
                    
                    msg = message_queue_.front();
                    message_queue_.pop();
                }
                
                // 处理消息
                process_message(msg);
            }
        });
        
        return true;
    }
    
    void stop() {
        running_ = false;
        cv_.notify_all();
        
        if (event_loop_thread_.joinable()) {
            event_loop_thread_.join();
        }
        
        // 从全局注册表移除本 Application 的所有痕迹，防止销毁后被回调（use-after-free）。
        auto& registry = ServiceRegistry::get_instance();
        registry.remove_handlers(this);
        
        std::map<service_t, std::set<instance_t>> offered_copy;
        {
            std::lock_guard<std::mutex> lock(services_mutex_);
            offered_copy.swap(offered_services_);
            requested_services_.clear();
        }
        for (auto& svc : offered_copy) {
            for (auto inst : svc.second) {
                registry.unregister_offerer(svc.first, inst, this);
                registry.unregister_service(svc.first, inst);
            }
        }
    }
    
    void offer_service(service_t service, instance_t instance) {
        {
            std::lock_guard<std::mutex> lock(services_mutex_);
            offered_services_[service].insert(instance);
        }
        
        // 使用全局服务注册表（注册可路由的服务端 + 触发可用性通知）
        ServiceRegistry::get_instance().register_offerer(service, instance, this);
        ServiceRegistry::get_instance().register_service(service, instance);
    }
    
    void stop_offer_service(service_t service, instance_t instance) {
        {
            std::lock_guard<std::mutex> lock(services_mutex_);
            if (offered_services_.count(service)) {
                offered_services_[service].erase(instance);
                if (offered_services_[service].empty()) {
                    offered_services_.erase(service);
                }
            }
        }
        
        // 使用全局服务注册表
        ServiceRegistry::get_instance().unregister_offerer(service, instance, this);
        ServiceRegistry::get_instance().unregister_service(service, instance);
    }
    
    void request_service(service_t service, instance_t instance) {
        {
            std::lock_guard<std::mutex> lock(services_mutex_);
            requested_services_[service].insert(instance);
        }
        
        // 注册到全局服务注册表，以便接收后续通知。
        // 以 this 作为 owner，析构时可精确移除本 Application 的回调。
        // 回调直接同步调用 notify_availability：
        //   - ServiceRegistry 已保证在其自身锁之外调用回调，此处无锁冲突；
        //   - 同步调用可确保 request_service 返回时 connected_ 已更新；
        //   - 避免 detach 线程在 Application 销毁后访问已释放成员（use-after-free）。
        ServiceRegistry::get_instance().register_availability_handler(
            service, instance, this,
            [this](service_t s, instance_t i, bool available) {
                notify_availability(s, i, available);
            });
    }
    
    void offer_method(service_t service, instance_t instance, method_t method) {
        std::lock_guard<std::mutex> lock(methods_mutex_);
        offered_methods_[{service, instance}].insert(method);
    }
    
    void register_message_handler(service_t service, instance_t instance, 
                                  method_t method, message_handler_t handler) {
        std::lock_guard<std::mutex> lock(handlers_mutex_);
        message_handlers_[{service, instance, method}] = handler;
    }
    
    void register_availability_handler(service_t service, instance_t instance,
                                       availability_handler_t handler) {
        {
            std::lock_guard<std::mutex> lock(handlers_mutex_);
            availability_handlers_[{service, instance}].push_back(handler);
        }
        
        // 如果服务已经可用，立即通知（在锁外调用，避免与 handler 内部逻辑发生锁冲突）
        if (ServiceRegistry::get_instance().is_service_available(service, instance)) {
            handler(service, instance, true);
        }
    }
    
    void send(const std::shared_ptr<Message>& message) {
        auto& registry = ServiceRegistry::get_instance();
        auto type = message->get_message_type();
        
        if (type == message_type_e::REQUEST ||
            type == message_type_e::REQUEST_NO_RETURN) {
            // 请求：标记发送者 client_id 以便响应回送，并投递到提供该服务的应用
            message->set_client(client_id_);
            Application* target =
                registry.find_offerer(message->get_service(), message->get_instance());
            if (target) {
                target->enqueue(message);
                return;
            }
            // 未找到服务端：回退到本地队列（保留旧行为）
            enqueue(message);
        } else {
            // 响应/通知：按请求方 client_id 回送到发起请求的应用
            Application* target = registry.find_application(message->get_client());
            if (target) {
                target->enqueue(message);
                return;
            }
            enqueue(message);
        }
    }
    
    // 将消息放入本应用的事件循环队列（供跨应用路由调用）
    void enqueue(const std::shared_ptr<Message>& message) {
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            message_queue_.push(message);
        }
        cv_.notify_one();
    }
    
    client_id_t get_client_id() const { return client_id_; }
    
    const std::string& get_name() const { return name_; }
    
private:
    std::string name_;
    std::atomic<bool> running_;
    std::thread event_loop_thread_;
    client_id_t client_id_;
    
    // 服务管理
    std::mutex services_mutex_;
    std::map<service_t, std::set<instance_t>> offered_services_;
    std::map<service_t, std::set<instance_t>> requested_services_;
    
    // 方法管理
    std::mutex methods_mutex_;
    std::map<std::pair<service_t, instance_t>, std::set<method_t>> offered_methods_;
    
    // 处理器
    std::mutex handlers_mutex_;
    std::map<std::tuple<service_t, instance_t, method_t>, message_handler_t> message_handlers_;
    std::map<std::pair<service_t, instance_t>, std::vector<availability_handler_t>> availability_handlers_;
    
    // 消息队列
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::queue<std::shared_ptr<Message>> message_queue_;
    
    bool is_service_offered(service_t service, instance_t instance) const {
        auto it = offered_services_.find(service);
        if (it != offered_services_.end()) {
            return it->second.count(instance) > 0;
        }
        return false;
    }
    
    void notify_availability(service_t service, instance_t instance, bool available) {
        std::vector<availability_handler_t> handlers;
        {
            std::lock_guard<std::mutex> lock(handlers_mutex_);
            auto key = std::make_pair(service, instance);
            auto it = availability_handlers_.find(key);
            if (it != availability_handlers_.end()) {
                handlers = it->second;
            }
        }
        
        // 在锁外调用处理器
        for (auto& handler : handlers) {
            handler(service, instance, available);
        }
    }
    
    void process_message(const std::shared_ptr<Message>& message) {
        // 查找消息处理器（先精确匹配 method，再回退到 ANY_METHOD）
        message_handler_t handler;
        {
            std::lock_guard<std::mutex> lock(handlers_mutex_);
            auto key = std::make_tuple(message->get_service(),
                                       message->get_instance(),
                                       message->get_method());
            auto it = message_handlers_.find(key);
            if (it == message_handlers_.end()) {
                auto any_key = std::make_tuple(message->get_service(),
                                               message->get_instance(),
                                               ANY_METHOD);
                it = message_handlers_.find(any_key);
            }
            if (it != message_handlers_.end()) {
                handler = it->second;
            }
        }
        
        // 在锁外调用处理器：服务端处理器内部会调用 send()，
        // 在锁外调用可避免潜在的锁顺序问题。
        if (handler) {
            handler(message);
        }
    }
};

// Runtime::create_application 实现
inline std::shared_ptr<Application> Runtime::create_application(const std::string& name) {
    return std::make_shared<Application>(name);
}

} // namespace mock_someip
