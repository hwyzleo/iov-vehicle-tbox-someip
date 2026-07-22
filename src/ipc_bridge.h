#pragma once

#include "tbox_someip/interfaces.h"
#include <memory>
#include <map>
#include <mutex>

namespace tbox {
namespace someip {

class IpcBridge {
public:
    IpcBridge();
    ~IpcBridge();
    
    // 初始化桥接层
    bool initialize();
    
    // 注册客户端
    bool registerClient(const std::string& name, std::shared_ptr<IIpcClient> client);
    
    // 获取客户端
    std::shared_ptr<IIpcClient> getClient(const std::string& name);
    
    // 关闭桥接层
    void shutdown();
    
    // 检查是否已初始化
    bool isInitialized() const;

private:
    bool initialized_;
    std::map<std::string, std::shared_ptr<IIpcClient>> clients_;
    mutable std::mutex mutex_;
};

} // namespace someip
} // namespace tbox
