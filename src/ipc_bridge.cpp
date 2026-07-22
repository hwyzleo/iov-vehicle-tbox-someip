#include "ipc_bridge.h"
#include <iostream>

namespace tbox {
namespace someip {

IpcBridge::IpcBridge() : initialized_(false) {}

IpcBridge::~IpcBridge() {
    shutdown();
}

bool IpcBridge::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        std::cerr << "IpcBridge already initialized" << std::endl;
        return false;
    }
    
    // 初始化 IPC 桥接层
    // TODO: 实现实际的 IPC 初始化逻辑
    
    initialized_ = true;
    std::cout << "IpcBridge initialized successfully" << std::endl;
    return true;
}

bool IpcBridge::registerClient(const std::string& name, std::shared_ptr<IIpcClient> client) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        std::cerr << "IpcBridge not initialized" << std::endl;
        return false;
    }
    
    if (!client) {
        std::cerr << "Invalid client pointer" << std::endl;
        return false;
    }
    
    if (clients_.find(name) != clients_.end()) {
        std::cerr << "Client '" << name << "' already registered" << std::endl;
        return false;
    }
    
    clients_[name] = client;
    std::cout << "Client '" << name << "' registered successfully" << std::endl;
    return true;
}

std::shared_ptr<IIpcClient> IpcBridge::getClient(const std::string& name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return nullptr;
    }
    
    auto it = clients_.find(name);
    if (it != clients_.end()) {
        return it->second;
    }
    
    return nullptr;
}

void IpcBridge::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return;
    }
    
    // 关闭所有客户端
    for (auto& pair : clients_) {
        if (pair.second) {
            pair.second->shutdown();
        }
    }
    clients_.clear();
    
    initialized_ = false;
    std::cout << "IpcBridge shutdown" << std::endl;
}

bool IpcBridge::isInitialized() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return initialized_;
}

} // namespace someip
} // namespace tbox
