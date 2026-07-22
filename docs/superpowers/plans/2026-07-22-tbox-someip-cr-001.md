# TBOX-SOMEIP-DSN-CR-001 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 实现 TBOX-SOMEIP 接入/适配层首版，作为 TBOX 面向车内 SOME/IP 网络的唯一门面服务。

**架构：** 常驻服务（daemon）架构，对内通过 IPC client 桩访问基础服务（GNSS/RTC/PROV/TSP），对外通过 vsomeip/CommonAPI 暴露 SOME/IP 服务（TimeSync/Location/NetStatus/RemoteCmdRelay），支持 SOME/IP-SD 服务发现。

**技术栈：** C++17, vsomeip, CommonAPI, systemd, CMake, Google Test

---

## 文件结构

```
iov-vehicle-tbox-someip/
├── CMakeLists.txt                    # 顶层构建配置
├── src/
│   ├── CMakeLists.txt               # 源码构建配置
│   ├── main.cpp                     # 服务入口点
│   ├── someip_daemon.h/cpp          # SOMEIP 守护进程主体
│   ├── service_discovery.h/cpp      # SOME/IP-SD 服务发现管理
│   ├── ipc_bridge.h/cpp             # IPC 桥接层
│   ├── service_registry.h/cpp       # 服务注册表
│   ├── error_codes.h                # 错误码定义
│   └── services/
│       ├── CMakeLists.txt           # 服务构建配置
│       ├── time_sync.h/cpp          # TimeSync 服务实现
│       ├── location.h/cpp           # Location 服务实现
│       ├── net_status.h/cpp         # NetStatus 服务实现
│       └── remote_cmd_relay.h/cpp   # RemoteCmdRelay 服务实现
├── include/
│   └── tbox_someip/
│       ├── types.h                  # 公共类型定义
│       ├── interfaces.h             # 服务接口定义
│       └── config.h                 # 配置定义
├── tests/
│   ├── CMakeLists.txt               # 测试构建配置
│   ├── unit/
│   │   ├── test_service_discovery.cpp
│   │   ├── test_ipc_bridge.cpp
│   │   ├── test_time_sync.cpp
│   │   ├── test_location.cpp
│   │   ├── test_net_status.cpp
│   │   └── test_remote_cmd_relay.cpp
│   └── integration/
│       └── test_someip_integration.cpp
├── config/
│   └── someip.json                  # vsomeip 配置文件
├── systemd/
│   └── tbox-someip.service          # systemd 服务文件
└── docs/
    └── api.md                       # API 文档
```

---

## 任务 1：项目基础结构与构建系统

**文件：**
- 创建：`CMakeLists.txt`
- 创建：`src/CMakeLists.txt`
- 创建：`include/tbox_someip/types.h`
- 创建：`include/tbox_someip/config.h`

- [ ] **步骤 1：创建顶层 CMakeLists.txt**

```cmake
cmake_minimum_required(VERSION 3.16)
project(tbox-someip VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 查找依赖
find_package(PkgConfig REQUIRED)
pkg_check_modules(VSOMEIP REQUIRED vsomeip3)
pkg_check_modules(COMMONAPI REQUIRED CommonAPI)
pkg_check_modules(COMMONAPI_SOMEIP REQUIRED CommonAPI-SomeIP)

# 选项
option(BUILD_TESTS "Build tests" ON)

# 子目录
add_subdirectory(src)

if(BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
```

- [ ] **步骤 2：创建源码 CMakeLists.txt**

```cmake
# 源文件列表
set(SOURCES
    main.cpp
    someip_daemon.cpp
    service_discovery.cpp
    ipc_bridge.cpp
    service_registry.cpp
    services/time_sync.cpp
    services/location.cpp
    services/net_status.cpp
    services/remote_cmd_relay.cpp
)

# 创建可执行文件
add_executable(tbox-someip ${SOURCES})

# 包含目录
target_include_directories(tbox-someip
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${VSOMEIP_INCLUDE_DIRS}
        ${COMMONAPI_INCLUDE_DIRS}
        ${COMMONAPI_SOMEIP_INCLUDE_DIRS}
)

# 链接库
target_link_libraries(tbox-someip
    PRIVATE
        ${VSOMEIP_LIBRARIES}
        ${COMMONAPI_LIBRARIES}
        ${COMMONAPI_SOMEIP_LIBRARIES}
        pthread
)

# 安装
install(TARGETS tbox-someip
    RUNTIME DESTINATION bin
)

install(FILES ${CMAKE_SOURCE_DIR}/systemd/tbox-someip.service
    DESTINATION /etc/systemd/system/
)
```

- [ ] **步骤 3：创建公共类型定义**

```cpp
// include/tbox_someip/types.h
#pragma once

#include <cstdint>
#include <string>
#include <functional>
#include <chrono>

namespace tbox {
namespace someip {

// 服务 ID 定义（草案，最终以整车服务矩阵标定为准）
namespace service_ids {
    constexpr uint16_t TIME_SYNC = 0x0100;
    constexpr uint16_t LOCATION = 0x0101;
    constexpr uint16_t NET_STATUS = 0x0102;
    constexpr uint16_t REMOTE_CMD_RELAY = 0x0103;
} // namespace service_ids

// 实例 ID 定义
namespace instance_ids {
    constexpr uint16_t DEFAULT = 0x0001;
} // namespace instance_ids

// 方法 ID 定义
namespace method_ids {
    // TimeSync
    constexpr uint16_t GET_TIME = 0x0001;
    
    // Location
    constexpr uint16_t GET_POSITION = 0x0001;
    
    // NetStatus
    constexpr uint16_t GET_NET_STATUS = 0x0001;
    
    // RemoteCmdRelay
    constexpr uint16_t ON_REMOTE_COMMAND = 0x0001;
} // namespace method_ids

// 事件 ID 定义
namespace event_ids {
    // TimeSync
    constexpr uint16_t ON_TIME_UPDATE = 0x8001;
    
    // Location
    constexpr uint16_t ON_POSITION = 0x8001;
    
    // NetStatus
    constexpr uint16_t ON_NET_STATUS_CHANGE = 0x8001;
    
    // RemoteCmdRelay
    constexpr uint16_t ON_REMOTE_COMMAND_NOTIFY = 0x8001;
} // namespace event_ids

// 时间同步数据结构
struct TimeData {
    std::chrono::system_clock::time_point timestamp;
    int64_t utc_seconds;
    int32_t utc_offset;
    bool is_valid;
};

// 位置数据结构
struct PositionData {
    double latitude;      // 纬度（度）
    double longitude;     // 经度（度）
    double altitude;      // 海拔（米）
    double speed;         // 车速（km/h）
    double heading;       // 航向（度）
    bool is_valid;
};

// 网络状态数据结构
struct NetStatusData {
    bool is_online;
    int32_t signal_quality;  // 信号质量 (0-100)
    std::string network_type; // 网络类型 (4G/5G/WiFi)
    bool is_valid;
};

// 远程命令数据结构
struct RemoteCommand {
    std::string command_id;
    std::string command_type;
    std::string payload;
    std::chrono::system_clock::time_point timestamp;
};

// 错误码定义
enum class ErrorCode : uint16_t {
    SUCCESS = 0,
    NETWORK_NOT_READY = 1001,      // 网络/端口未就绪，SD 发布失败
    CLIENT_CALL_TIMEOUT = 1002,    // 内部 client 调用超时
    CLIENT_CALL_FAILED = 1003,     // 内部 client 调用失败/不可用
    SERIALIZATION_FAILED = 1004,   // 序列化/映射转换失败
};

// 回调类型定义
using TimeUpdateCallback = std::function<void(const TimeData&)>;
using PositionCallback = std::function<void(const PositionData&)>;
using NetStatusCallback = std::function<void(const NetStatusData&)>;
using RemoteCommandCallback = std::function<void(const RemoteCommand&)>;

// 服务状态
enum class ServiceState {
    UNAVAILABLE,    // 不可用
    INITIALIZING,   // 初始化中
    AVAILABLE,      // 可用
    ERROR           // 错误
};

} // namespace someip
} // namespace tbox
```

- [ ] **步骤 4：创建配置定义**

```cpp
// include/tbox_someip/config.h
#pragma once

#include <string>
#include <cstdint>

namespace tbox {
namespace someip {

// 服务配置
struct ServiceConfig {
    // 网络配置
    std::string network_interface = "eth0";
    uint16_t sd_port = 30490;
    
    // 服务发现配置
    bool enable_sd = true;
    uint32_t sd_offer_interval_ms = 1000;
    uint32_t sd_request_timeout_ms = 5000;
    
    // IPC 配置
    uint32_t ipc_timeout_ms = 1000;
    
    // 日志配置
    std::string log_level = "info";
    std::string log_file = "/var/log/tbox-someip.log";
    
    // 服务实例配置
    bool enable_time_sync = true;
    bool enable_location = true;
    bool enable_net_status = true;
    bool enable_remote_cmd_relay = true;
};

// 默认配置
inline ServiceConfig getDefaultConfig() {
    return ServiceConfig{};
}

} // namespace someip
} // namespace tbox
```

- [ ] **步骤 5：验证构建系统**

运行：`mkdir -p build && cd build && cmake .. -DBUILD_TESTS=OFF`
预期：CMake 配置成功，生成 build 目录

---

## 任务 2：错误码与接口定义

**文件：**
- 创建：`src/error_codes.h`
- 创建：`include/tbox_someip/interfaces.h`

- [ ] **步骤 1：创建错误码定义**

```cpp
// src/error_codes.h
#pragma once

#include "tbox_someip/types.h"
#include <string>
#include <map>

namespace tbox {
namespace someip {

// 错误码工具类
class ErrorCodeHelper {
public:
    static std::string toString(ErrorCode code) {
        static const std::map<ErrorCode, std::string> error_map = {
            {ErrorCode::SUCCESS, "Success"},
            {ErrorCode::NETWORK_NOT_READY, "Network/port not ready, SD publish failed"},
            {ErrorCode::CLIENT_CALL_TIMEOUT, "Internal client call timeout"},
            {ErrorCode::CLIENT_CALL_FAILED, "Internal client call failed/unavailable"},
            {ErrorCode::SERIALIZATION_FAILED, "Serialization/mapping conversion failed"},
        };
        
        auto it = error_map.find(code);
        if (it != error_map.end()) {
            return it->second;
        }
        return "Unknown error";
    }
    
    static uint16_t toInt(ErrorCode code) {
        return static_cast<uint16_t>(code);
    }
};

} // namespace someip
} // namespace tbox
```

- [ ] **步骤 2：创建服务接口定义**

```cpp
// include/tbox_someip/interfaces.h
#pragma once

#include "types.h"
#include <functional>

namespace tbox {
namespace someip {

// IPC Client 接口
class IIpcClient {
public:
    virtual ~IIpcClient() = default;
    
    // 初始化客户端
    virtual bool initialize() = 0;
    
    // 检查连接状态
    virtual bool isConnected() const = 0;
    
    // 关闭客户端
    virtual void shutdown() = 0;
};

// GNSS Client 接口
class IGnssClient : public IIpcClient {
public:
    // 获取当前位置
    virtual ErrorCode getPosition(PositionData& position) = 0;
    
    // 订阅位置更新
    virtual void subscribePositionUpdates(PositionCallback callback) = 0;
};

// RTC Client 接口
class IRtcClient : public IIpcClient {
public:
    // 获取当前时间
    virtual ErrorCode getTime(TimeData& time) = 0;
    
    // 订阅时间更新
    virtual void subscribeTimeUpdates(TimeUpdateCallback callback) = 0;
};

// TSP/MQTT Client 接口
class ITspClient : public IIpcClient {
public:
    // 获取网络状态
    virtual ErrorCode getNetStatus(NetStatusData& status) = 0;
    
    // 订阅网络状态变化
    virtual void subscribeNetStatusChanges(NetStatusCallback callback) = 0;
    
    // 订阅远程命令
    virtual void subscribeRemoteCommands(RemoteCommandCallback callback) = 0;
};

// PROV Client 接口
class IProvClient : public IIpcClient {
public:
    // 获取配置
    virtual std::string getConfiguration(const std::string& key) = 0;
};

// SOME/IP 服务接口
class ISomeipService {
public:
    virtual ~ISomeipService() = default;
    
    // 初始化服务
    virtual bool initialize() = 0;
    
    // 启动服务
    virtual bool start() = 0;
    
    // 停止服务
    virtual void stop() = 0;
    
    // 获取服务状态
    virtual ServiceState getState() const = 0;
    
    // 获取服务 ID
    virtual uint16_t getServiceId() const = 0;
    
    // 获取实例 ID
    virtual uint16_t getInstanceId() const = 0;
};

// 服务发现接口
class IServiceDiscovery {
public:
    virtual ~IServiceDiscovery() = default;
    
    // 初始化服务发现
    virtual bool initialize() = 0;
    
    // 发布服务
    virtual bool offerService(uint16_t service_id, uint16_t instance_id) = 0;
    
    // 停止发布服务
    virtual void stopOfferService(uint16_t service_id, uint16_t instance_id) = 0;
    
    // 订阅服务可用性
    virtual void subscribeServiceAvailability(
        uint16_t service_id,
        uint16_t instance_id,
        std::function<void(bool)> callback
    ) = 0;
    
    // 关闭服务发现
    virtual void shutdown() = 0;
};

} // namespace someip
} // namespace tbox
```

- [ ] **步骤 3：验证接口完整性**

运行：`grep -r "virtual.*= 0" include/`
预期：所有接口都有纯虚函数定义

---

## 任务 3：IPC Bridge 实现

**文件：**
- 创建：`src/ipc_bridge.h`
- 创建：`src/ipc_bridge.cpp`

- [ ] **步骤 1：编写失败的测试**

```cpp
// tests/unit/test_ipc_bridge.cpp
#include <gtest/gtest.h>
#include "ipc_bridge.h"

using namespace tbox::someip;

class IpcBridgeTest : public ::testing::Test {
protected:
    void SetUp() override {
        bridge = std::make_unique<IpcBridge>();
    }
    
    void TearDown() override {
        if (bridge) {
            bridge->shutdown();
        }
    }
    
    std::unique_ptr<IpcBridge> bridge;
};

TEST_F(IpcBridgeTest, InitializeSuccess) {
    // 测试初始化
    EXPECT_TRUE(bridge->initialize());
}

TEST_F(IpcBridgeTest, InitializeFailure) {
    // 测试重复初始化
    EXPECT_TRUE(bridge->initialize());
    EXPECT_FALSE(bridge->initialize());
}

TEST_F(IpcBridgeTest, RegisterClient) {
    // 测试注册客户端
    EXPECT_TRUE(bridge->initialize());
    
    // 创建模拟客户端
    auto mock_client = std::make_shared<MockGnssClient>();
    EXPECT_TRUE(bridge->registerClient("gnss", mock_client));
}

TEST_F(IpcBridgeTest, GetClient) {
    // 测试获取客户端
    EXPECT_TRUE(bridge->initialize());
    
    auto mock_client = std::make_shared<MockGnssClient>();
    bridge->registerClient("gnss", mock_client);
    
    auto retrieved = bridge->getClient("gnss");
    EXPECT_EQ(retrieved, mock_client);
}

TEST_F(IpcBridgeTest, GetNonexistentClient) {
    // 测试获取不存在的客户端
    EXPECT_TRUE(bridge->initialize());
    
    auto retrieved = bridge->getClient("nonexistent");
    EXPECT_EQ(retrieved, nullptr);
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ./tests/unit/test_ipc_bridge`
预期：编译失败，报错 "IpcBridge 类未定义"

- [ ] **步骤 3：编写最少实现代码**

```cpp
// src/ipc_bridge.h
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
```

```cpp
// src/ipc_bridge.cpp
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
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ./tests/unit/test_ipc_bridge`
预期：所有测试通过

- [ ] **步骤 5：Commit**

```bash
git add src/ipc_bridge.h src/ipc_bridge.cpp tests/unit/test_ipc_bridge.cpp
git commit -m "feat: implement IpcBridge for internal service communication"
```

---

## 任务 4：服务发现实现

**文件：**
- 创建：`src/service_discovery.h`
- 创建：`src/service_discovery.cpp`

- [ ] **步骤 1：编写失败的测试**

```cpp
// tests/unit/test_service_discovery.cpp
#include <gtest/gtest.h>
#include "service_discovery.h"

using namespace tbox::someip;

class ServiceDiscoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        sd = std::make_unique<ServiceDiscovery>();
    }
    
    void TearDown() override {
        if (sd) {
            sd->shutdown();
        }
    }
    
    std::unique_ptr<ServiceDiscovery> sd;
};

TEST_F(ServiceDiscoveryTest, InitializeSuccess) {
    EXPECT_TRUE(sd->initialize());
}

TEST_F(ServiceDiscoveryTest, OfferServiceSuccess) {
    EXPECT_TRUE(sd->initialize());
    EXPECT_TRUE(sd->offerService(service_ids::TIME_SYNC, instance_ids::DEFAULT));
}

TEST_F(ServiceDiscoveryTest, OfferServiceBeforeInit) {
    EXPECT_FALSE(sd->offerService(service_ids::TIME_SYNC, instance_ids::DEFAULT));
}

TEST_F(ServiceDiscoveryTest, StopOfferService) {
    EXPECT_TRUE(sd->initialize());
    EXPECT_TRUE(sd->offerService(service_ids::TIME_SYNC, instance_ids::DEFAULT));
    
    // 停止发布不应崩溃
    sd->stopOfferService(service_ids::TIME_SYNC, instance_ids::DEFAULT);
}

TEST_F(ServiceDiscoveryTest, SubscribeServiceAvailability) {
    EXPECT_TRUE(sd->initialize());
    
    bool available = false;
    sd->subscribeServiceAvailability(
        service_ids::TIME_SYNC,
        instance_ids::DEFAULT,
        [&available](bool avail) { available = avail; }
    );
    
    // 订阅不应崩溃
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ./tests/unit/test_service_discovery`
预期：编译失败，报错 "ServiceDiscovery 类未定义"

- [ ] **步骤 3：编写最少实现代码**

```cpp
// src/service_discovery.h
#pragma once

#include "tbox_someip/interfaces.h"
#include <vsomeip/vsomeip.hpp>
#include <memory>
#include <map>
#include <mutex>
#include <functional>

namespace tbox {
namespace someip {

class ServiceDiscovery : public IServiceDiscovery {
public:
    ServiceDiscovery();
    ~ServiceDiscovery() override;
    
    bool initialize() override;
    bool offerService(uint16_t service_id, uint16_t instance_id) override;
    void stopOfferService(uint16_t service_id, uint16_t instance_id) override;
    void subscribeServiceAvailability(
        uint16_t service_id,
        uint16_t instance_id,
        std::function<void(bool)> callback
    ) override;
    void shutdown() override;

private:
    void onServiceAvailability(vsomeip::service_t service_id, 
                              vsomeip::instance_t instance_id, 
                              bool is_available);
    
    bool initialized_;
    std::shared_ptr<vsomeip::application> app_;
    std::map<std::pair<uint16_t, uint16_t>, std::function<void(bool)>> availability_callbacks_;
    mutable std::mutex mutex_;
};

} // namespace someip
} // namespace tbox
```

```cpp
// src/service_discovery.cpp
#include "service_discovery.h"
#include <iostream>

namespace tbox {
namespace someip {

ServiceDiscovery::ServiceDiscovery() : initialized_(false) {}

ServiceDiscovery::~ServiceDiscovery() {
    shutdown();
}

bool ServiceDiscovery::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        std::cerr << "ServiceDiscovery already initialized" << std::endl;
        return false;
    }
    
    try {
        // 创建 vsomeip 应用
        app_ = vsomeip::runtime::get()->create_application("tbox-someip-sd");
        
        // 初始化应用
        if (!app_->init()) {
            std::cerr << "Failed to initialize vsomeip application" << std::endl;
            return false;
        }
        
        // 注册服务可用性回调
        app_->register_availability_handler(
            vsomeip::ANY_SERVICE,
            vsomeip::ANY_INSTANCE,
            std::bind(&ServiceDiscovery::onServiceAvailability, this,
                     std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)
        );
        
        // 启动应用
        app_->start();
        
        initialized_ = true;
        std::cout << "ServiceDiscovery initialized successfully" << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to initialize ServiceDiscovery: " << e.what() << std::endl;
        return false;
    }
}

bool ServiceDiscovery::offerService(uint16_t service_id, uint16_t instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_ || !app_) {
        std::cerr << "ServiceDiscovery not initialized" << std::endl;
        return false;
    }
    
    try {
        // 发布服务
        app_->offer_service(service_id, instance_id);
        std::cout << "Offering service: 0x" << std::hex << service_id 
                  << " instance: 0x" << instance_id << std::endl;
        return true;
        
    } catch (const std::exception& e) {
        std::cerr << "Failed to offer service: " << e.what() << std::endl;
        return false;
    }
}

void ServiceDiscovery::stopOfferService(uint16_t service_id, uint16_t instance_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_ || !app_) {
        return;
    }
    
    try {
        app_->stop_offer_service(service_id, instance_id);
        std::cout << "Stopped offering service: 0x" << std::hex << service_id 
                  << " instance: 0x" << instance_id << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Failed to stop offering service: " << e.what() << std::endl;
    }
}

void ServiceDiscovery::subscribeServiceAvailability(
    uint16_t service_id,
    uint16_t instance_id,
    std::function<void(bool)> callback) {
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_ || !app_) {
        std::cerr << "ServiceDiscovery not initialized" << std::endl;
        return;
    }
    
    // 保存回调
    availability_callbacks_[{service_id, instance_id}] = callback;
    
    // 请求服务通知
    app_->request_service(service_id, instance_id);
    
    std::cout << "Subscribed to service availability: 0x" << std::hex << service_id 
              << " instance: 0x" << instance_id << std::endl;
}

void ServiceDiscovery::onServiceAvailability(vsomeip::service_t service_id, 
                                            vsomeip::instance_t instance_id, 
                                            bool is_available) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto key = std::make_pair(service_id, instance_id);
    auto it = availability_callbacks_.find(key);
    
    if (it != availability_callbacks_.end()) {
        std::cout << "Service availability changed: 0x" << std::hex << service_id 
                  << " instance: 0x" << instance_id 
                  << " available: " << is_available << std::endl;
        
        it->second(is_available);
    }
}

void ServiceDiscovery::shutdown() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        return;
    }
    
    if (app_) {
        app_->stop();
        app_.reset();
    }
    
    availability_callbacks_.clear();
    initialized_ = false;
    
    std::cout << "ServiceDiscovery shutdown" << std::endl;
}

} // namespace someip
} // namespace tbox
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ./tests/unit/test_service_discovery`
预期：所有测试通过

- [ ] **步骤 5：Commit**

```bash
git add src/service_discovery.h src/service_discovery.cpp tests/unit/test_service_discovery.cpp
git commit -m "feat: implement ServiceDiscovery with SOME/IP-SD support"
```

---

## 任务 5：TimeSync 服务实现

**文件：**
- 创建：`src/services/time_sync.h`
- 创建：`src/services/time_sync.cpp`

- [ ] **步骤 1：编写失败的测试**

```cpp
// tests/unit/test_time_sync.cpp
#include <gtest/gtest.h>
#include "services/time_sync.h"
#include "ipc_bridge.h"

using namespace tbox::someip;

class TimeSyncTest : public ::testing::Test {
protected:
    void SetUp() override {
        bridge = std::make_unique<IpcBridge>();
        bridge->initialize();
        
        time_sync = std::make_unique<TimeSyncService>(bridge.get());
    }
    
    void TearDown() override {
        if (time_sync) {
            time_sync->stop();
        }
        if (bridge) {
            bridge->shutdown();
        }
    }
    
    std::unique_ptr<IpcBridge> bridge;
    std::unique_ptr<TimeSyncService> time_sync;
};

TEST_F(TimeSyncTest, InitializeSuccess) {
    EXPECT_TRUE(time_sync->initialize());
}

TEST_F(TimeSyncTest, StartWithoutInit) {
    EXPECT_FALSE(time_sync->start());
}

TEST_F(TimeSyncTest, GetTimeSuccess) {
    EXPECT_TRUE(time_sync->initialize());
    EXPECT_TRUE(time_sync->start());
    
    TimeData time;
    EXPECT_EQ(time_sync->getTime(time), ErrorCode::SUCCESS);
    EXPECT_TRUE(time.is_valid);
}

TEST_F(TimeSyncTest, GetTimeBeforeStart) {
    EXPECT_TRUE(time_sync->initialize());
    
    TimeData time;
    EXPECT_EQ(time_sync->getTime(time), ErrorCode::CLIENT_CALL_FAILED);
    EXPECT_FALSE(time.is_valid);
}

TEST_F(TimeSyncTest, SubscribeTimeUpdates) {
    EXPECT_TRUE(time_sync->initialize());
    EXPECT_TRUE(time_sync->start());
    
    bool received = false;
    time_sync->subscribeTimeUpdates([&received](const TimeData&) {
        received = true;
    });
    
    // 订阅不应崩溃
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ./tests/unit/test_time_sync`
预期：编译失败，报错 "TimeSyncService 类未定义"

- [ ] **步骤 3：编写最少实现代码**

```cpp
// src/services/time_sync.h
#pragma once

#include "tbox_someip/interfaces.h"
#include <vsomeip/vsomeip.hpp>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

namespace tbox {
namespace someip {

class IpcBridge;

class TimeSyncService : public ISomeipService {
public:
    TimeSyncService(IpcBridge* bridge);
    ~TimeSyncService() override;
    
    bool initialize() override;
    bool start() override;
    void stop() override;
    ServiceState getState() const override;
    uint16_t getServiceId() const override;
    uint16_t getInstanceId() const override;
    
    // TimeSync 特定方法
    ErrorCode getTime(TimeData& time);
    void subscribeTimeUpdates(TimeUpdateCallback callback);

private:
    void run();
    void handleGetTime(const std::shared_ptr<vsomeip::message>& request);
    void notifyTimeUpdate(const TimeData& time);
    
    IpcBridge* bridge_;
    ServiceState state_;
    std::shared_ptr<vsomeip::application> app_;
    std::shared_ptr<IRtcClient> rtc_client_;
    
    std::vector<TimeUpdateCallback> time_callbacks_;
    mutable std::mutex mutex_;
    
    std::atomic<bool> running_;
    std::thread worker_thread_;
};

} // namespace someip
} // namespace tbox
```

```cpp
// src/services/time_sync.cpp
#include "time_sync.h"
#include "ipc_bridge.h"
#include <iostream>
#include <chrono>

namespace tbox {
namespace someip {

TimeSyncService::TimeSyncService(IpcBridge* bridge) 
    : bridge_(bridge)
    , state_(ServiceState::UNAVAILABLE)
    , running_(false) {}

TimeSyncService::~TimeSyncService() {
    stop();
}

bool TimeSyncService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::UNAVAILABLE) {
        std::cerr << "TimeSyncService already initialized" << std::endl;
        return false;
    }
    
    if (!bridge_ || !bridge_->isInitialized()) {
        std::cerr << "IpcBridge not initialized" << std::endl;
        return false;
    }
    
    // 获取 RTC 客户端
    auto client = bridge_->getClient("rtc");
    if (!client) {
        std::cerr << "RTC client not found" << std::endl;
        return false;
    }
    
    rtc_client_ = std::dynamic_pointer_cast<IRtcClient>(client);
    if (!rtc_client_) {
        std::cerr << "Invalid RTC client type" << std::endl;
        return false;
    }
    
    // 初始化 RTC 客户端
    if (!rtc_client_->initialize()) {
        std::cerr << "Failed to initialize RTC client" << std::endl;
        return false;
    }
    
    state_ = ServiceState::INITIALIZING;
    std::cout << "TimeSyncService initialized" << std::endl;
    return true;
}

bool TimeSyncService::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::INITIALIZING) {
        std::cerr << "TimeSyncService not in initializing state" << std::endl;
        return false;
    }
    
    // 创建 vsomeip 应用
    app_ = vsomeip::runtime::get()->create_application("tbox-time-sync");
    
    // 注册请求处理器
    app_->register_request_handler(
        service_ids::TIME_SYNC,
        instance_ids::DEFAULT,
        method_ids::GET_TIME,
        std::bind(&TimeSyncService::handleGetTime, this, std::placeholders::_1)
    );
    
    // 初始化应用
    if (!app_->init()) {
        std::cerr << "Failed to initialize vsomeip application" << std::endl;
        return false;
    }
    
    // 启动应用
    app_->start();
    
    // 启动工作线程
    running_ = true;
    worker_thread_ = std::thread(&TimeSyncService::run, this);
    
    state_ = ServiceState::AVAILABLE;
    std::cout << "TimeSyncService started" << std::endl;
    return true;
}

void TimeSyncService::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ == ServiceState::UNAVAILABLE) {
            return;
        }
        
        running_ = false;
    }
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (app_) {
        app_->stop();
        app_.reset();
    }
    
    if (rtc_client_) {
        rtc_client_->shutdown();
        rtc_client_.reset();
    }
    
    time_callbacks_.clear();
    state_ = ServiceState::UNAVAILABLE;
    
    std::cout << "TimeSyncService stopped" << std::endl;
}

ServiceState TimeSyncService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

uint16_t TimeSyncService::getServiceId() const {
    return service_ids::TIME_SYNC;
}

uint16_t TimeSyncService::getInstanceId() const {
    return instance_ids::DEFAULT;
}

ErrorCode TimeSyncService::getTime(TimeData& time) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::AVAILABLE) {
        return ErrorCode::CLIENT_CALL_FAILED;
    }
    
    if (!rtc_client_ || !rtc_client_->isConnected()) {
        return ErrorCode::CLIENT_CALL_FAILED;
    }
    
    return rtc_client_->getTime(time);
}

void TimeSyncService::subscribeTimeUpdates(TimeUpdateCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    time_callbacks_.push_back(callback);
    
    // 订阅 RTC 时间更新
    if (rtc_client_) {
        rtc_client_->subscribeTimeUpdates([this](const TimeData& time) {
            notifyTimeUpdate(time);
        });
    }
}

void TimeSyncService::handleGetTime(const std::shared_ptr<vsomeip::message>& request) {
    // 获取时间
    TimeData time;
    ErrorCode result = getTime(time);
    
    // 创建响应
    auto response = vsomeip::runtime::get()->create_response(request);
    
    // 序列化响应
    // TODO: 实现实际的序列化逻辑
    
    // 发送响应
    app_->send(response);
}

void TimeSyncService::notifyTimeUpdate(const TimeData& time) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& callback : time_callbacks_) {
        if (callback) {
            callback(time);
        }
    }
}

void TimeSyncService::run() {
    while (running_) {
        // 定期更新时间
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

} // namespace someip
} // namespace tbox
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ./tests/unit/test_time_sync`
预期：所有测试通过

- [ ] **步骤 5：Commit**

```bash
git add src/services/time_sync.h src/services/time_sync.cpp tests/unit/test_time_sync.cpp
git commit -m "feat: implement TimeSync service for time synchronization"
```

---

## 任务 6：Location 服务实现

**文件：**
- 创建：`src/services/location.h`
- 创建：`src/services/location.cpp`

- [ ] **步骤 1：编写失败的测试**

```cpp
// tests/unit/test_location.cpp
#include <gtest/gtest.h>
#include "services/location.h"
#include "ipc_bridge.h"

using namespace tbox::someip;

class LocationTest : public ::testing::Test {
protected:
    void SetUp() override {
        bridge = std::make_unique<IpcBridge>();
        bridge->initialize();
        
        location = std::make_unique<LocationService>(bridge.get());
    }
    
    void TearDown() override {
        if (location) {
            location->stop();
        }
        if (bridge) {
            bridge->shutdown();
        }
    }
    
    std::unique_ptr<IpcBridge> bridge;
    std::unique_ptr<LocationService> location;
};

TEST_F(LocationTest, InitializeSuccess) {
    EXPECT_TRUE(location->initialize());
}

TEST_F(LocationTest, GetPositionSuccess) {
    EXPECT_TRUE(location->initialize());
    EXPECT_TRUE(location->start());
    
    PositionData position;
    EXPECT_EQ(location->getPosition(position), ErrorCode::SUCCESS);
    EXPECT_TRUE(position.is_valid);
}

TEST_F(LocationTest, SubscribePositionUpdates) {
    EXPECT_TRUE(location->initialize());
    EXPECT_TRUE(location->start());
    
    bool received = false;
    location->subscribePositionUpdates([&received](const PositionData&) {
        received = true;
    });
    
    // 订阅不应崩溃
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ./tests/unit/test_location`
预期：编译失败，报错 "LocationService 类未定义"

- [ ] **步骤 3：编写最少实现代码**

```cpp
// src/services/location.h
#pragma once

#include "tbox_someip/interfaces.h"
#include <vsomeip/vsomeip.hpp>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

namespace tbox {
namespace someip {

class IpcBridge;

class LocationService : public ISomeipService {
public:
    LocationService(IpcBridge* bridge);
    ~LocationService() override;
    
    bool initialize() override;
    bool start() override;
    void stop() override;
    ServiceState getState() const override;
    uint16_t getServiceId() const override;
    uint16_t getInstanceId() const override;
    
    // Location 特定方法
    ErrorCode getPosition(PositionData& position);
    void subscribePositionUpdates(PositionCallback callback);

private:
    void run();
    void handleGetPosition(const std::shared_ptr<vsomeip::message>& request);
    void notifyPositionUpdate(const PositionData& position);
    
    IpcBridge* bridge_;
    ServiceState state_;
    std::shared_ptr<vsomeip::application> app_;
    std::shared_ptr<IGnssClient> gnss_client_;
    
    std::vector<PositionCallback> position_callbacks_;
    mutable std::mutex mutex_;
    
    std::atomic<bool> running_;
    std::thread worker_thread_;
};

} // namespace someip
} // namespace tbox
```

```cpp
// src/services/location.cpp
#include "location.h"
#include "ipc_bridge.h"
#include <iostream>
#include <chrono>

namespace tbox {
namespace someip {

LocationService::LocationService(IpcBridge* bridge) 
    : bridge_(bridge)
    , state_(ServiceState::UNAVAILABLE)
    , running_(false) {}

LocationService::~LocationService() {
    stop();
}

bool LocationService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::UNAVAILABLE) {
        std::cerr << "LocationService already initialized" << std::endl;
        return false;
    }
    
    if (!bridge_ || !bridge_->isInitialized()) {
        std::cerr << "IpcBridge not initialized" << std::endl;
        return false;
    }
    
    // 获取 GNSS 客户端
    auto client = bridge_->getClient("gnss");
    if (!client) {
        std::cerr << "GNSS client not found" << std::endl;
        return false;
    }
    
    gnss_client_ = std::dynamic_pointer_cast<IGnssClient>(client);
    if (!gnss_client_) {
        std::cerr << "Invalid GNSS client type" << std::endl;
        return false;
    }
    
    // 初始化 GNSS 客户端
    if (!gnss_client_->initialize()) {
        std::cerr << "Failed to initialize GNSS client" << std::endl;
        return false;
    }
    
    state_ = ServiceState::INITIALIZING;
    std::cout << "LocationService initialized" << std::endl;
    return true;
}

bool LocationService::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::INITIALIZING) {
        std::cerr << "LocationService not in initializing state" << std::endl;
        return false;
    }
    
    // 创建 vsomeip 应用
    app_ = vsomeip::runtime::get()->create_application("tbox-location");
    
    // 注册请求处理器
    app_->register_request_handler(
        service_ids::LOCATION,
        instance_ids::DEFAULT,
        method_ids::GET_POSITION,
        std::bind(&LocationService::handleGetPosition, this, std::placeholders::_1)
    );
    
    // 初始化应用
    if (!app_->init()) {
        std::cerr << "Failed to initialize vsomeip application" << std::endl;
        return false;
    }
    
    // 启动应用
    app_->start();
    
    // 启动工作线程
    running_ = true;
    worker_thread_ = std::thread(&LocationService::run, this);
    
    state_ = ServiceState::AVAILABLE;
    std::cout << "LocationService started" << std::endl;
    return true;
}

void LocationService::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ == ServiceState::UNAVAILABLE) {
            return;
        }
        
        running_ = false;
    }
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (app_) {
        app_->stop();
        app_.reset();
    }
    
    if (gnss_client_) {
        gnss_client_->shutdown();
        gnss_client_.reset();
    }
    
    position_callbacks_.clear();
    state_ = ServiceState::UNAVAILABLE;
    
    std::cout << "LocationService stopped" << std::endl;
}

ServiceState LocationService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

uint16_t LocationService::getServiceId() const {
    return service_ids::LOCATION;
}

uint16_t LocationService::getInstanceId() const {
    return instance_ids::DEFAULT;
}

ErrorCode LocationService::getPosition(PositionData& position) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::AVAILABLE) {
        return ErrorCode::CLIENT_CALL_FAILED;
    }
    
    if (!gnss_client_ || !gnss_client_->isConnected()) {
        return ErrorCode::CLIENT_CALL_FAILED;
    }
    
    return gnss_client_->getPosition(position);
}

void LocationService::subscribePositionUpdates(PositionCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    position_callbacks_.push_back(callback);
    
    // 订阅 GNSS 位置更新
    if (gnss_client_) {
        gnss_client_->subscribePositionUpdates([this](const PositionData& position) {
            notifyPositionUpdate(position);
        });
    }
}

void LocationService::handleGetPosition(const std::shared_ptr<vsomeip::message>& request) {
    // 获取位置
    PositionData position;
    ErrorCode result = getPosition(position);
    
    // 创建响应
    auto response = vsomeip::runtime::get()->create_response(request);
    
    // 序列化响应
    // TODO: 实现实际的序列化逻辑
    
    // 发送响应
    app_->send(response);
}

void LocationService::notifyPositionUpdate(const PositionData& position) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& callback : position_callbacks_) {
        if (callback) {
            callback(position);
        }
    }
}

void LocationService::run() {
    while (running_) {
        // 定期更新位置
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace someip
} // namespace tbox
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ./tests/unit/test_location`
预期：所有测试通过

- [ ] **步骤 5：Commit**

```bash
git add src/services/location.h src/services/location.cpp tests/unit/test_location.cpp
git commit -m "feat: implement Location service for GNSS positioning"
```

---

## 任务 7：NetStatus 服务实现

**文件：**
- 创建：`src/services/net_status.h`
- 创建：`src/services/net_status.cpp`

- [ ] **步骤 1：编写失败的测试**

```cpp
// tests/unit/test_net_status.cpp
#include <gtest/gtest.h>
#include "services/net_status.h"
#include "ipc_bridge.h"

using namespace tbox::someip;

class NetStatusTest : public ::testing::Test {
protected:
    void SetUp() override {
        bridge = std::make_unique<IpcBridge>();
        bridge->initialize();
        
        net_status = std::make_unique<NetStatusService>(bridge.get());
    }
    
    void TearDown() override {
        if (net_status) {
            net_status->stop();
        }
        if (bridge) {
            bridge->shutdown();
        }
    }
    
    std::unique_ptr<IpcBridge> bridge;
    std::unique_ptr<NetStatusService> net_status;
};

TEST_F(NetStatusTest, InitializeSuccess) {
    EXPECT_TRUE(net_status->initialize());
}

TEST_F(NetStatusTest, GetNetStatusSuccess) {
    EXPECT_TRUE(net_status->initialize());
    EXPECT_TRUE(net_status->start());
    
    NetStatusData status;
    EXPECT_EQ(net_status->getNetStatus(status), ErrorCode::SUCCESS);
    EXPECT_TRUE(status.is_valid);
}

TEST_F(NetStatusTest, SubscribeNetStatusChanges) {
    EXPECT_TRUE(net_status->initialize());
    EXPECT_TRUE(net_status->start());
    
    bool received = false;
    net_status->subscribeNetStatusChanges([&received](const NetStatusData&) {
        received = true;
    });
    
    // 订阅不应崩溃
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ./tests/unit/test_net_status`
预期：编译失败，报错 "NetStatusService 类未定义"

- [ ] **步骤 3：编写最少实现代码**

```cpp
// src/services/net_status.h
#pragma once

#include "tbox_someip/interfaces.h"
#include <vsomeip/vsomeip.hpp>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

namespace tbox {
namespace someip {

class IpcBridge;

class NetStatusService : public ISomeipService {
public:
    NetStatusService(IpcBridge* bridge);
    ~NetStatusService() override;
    
    bool initialize() override;
    bool start() override;
    void stop() override;
    ServiceState getState() const override;
    uint16_t getServiceId() const override;
    uint16_t getInstanceId() const override;
    
    // NetStatus 特定方法
    ErrorCode getNetStatus(NetStatusData& status);
    void subscribeNetStatusChanges(NetStatusCallback callback);

private:
    void run();
    void handleGetNetStatus(const std::shared_ptr<vsomeip::message>& request);
    void notifyNetStatusChange(const NetStatusData& status);
    
    IpcBridge* bridge_;
    ServiceState state_;
    std::shared_ptr<vsomeip::application> app_;
    std::shared_ptr<ITspClient> tsp_client_;
    
    std::vector<NetStatusCallback> status_callbacks_;
    mutable std::mutex mutex_;
    
    std::atomic<bool> running_;
    std::thread worker_thread_;
};

} // namespace someip
} // namespace tbox
```

```cpp
// src/services/net_status.cpp
#include "net_status.h"
#include "ipc_bridge.h"
#include <iostream>
#include <chrono>

namespace tbox {
namespace someip {

NetStatusService::NetStatusService(IpcBridge* bridge) 
    : bridge_(bridge)
    , state_(ServiceState::UNAVAILABLE)
    , running_(false) {}

NetStatusService::~NetStatusService() {
    stop();
}

bool NetStatusService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::UNAVAILABLE) {
        std::cerr << "NetStatusService already initialized" << std::endl;
        return false;
    }
    
    if (!bridge_ || !bridge_->isInitialized()) {
        std::cerr << "IpcBridge not initialized" << std::endl;
        return false;
    }
    
    // 获取 TSP 客户端
    auto client = bridge_->getClient("tsp");
    if (!client) {
        std::cerr << "TSP client not found" << std::endl;
        return false;
    }
    
    tsp_client_ = std::dynamic_pointer_cast<ITspClient>(client);
    if (!tsp_client_) {
        std::cerr << "Invalid TSP client type" << std::endl;
        return false;
    }
    
    // 初始化 TSP 客户端
    if (!tsp_client_->initialize()) {
        std::cerr << "Failed to initialize TSP client" << std::endl;
        return false;
    }
    
    state_ = ServiceState::INITIALIZING;
    std::cout << "NetStatusService initialized" << std::endl;
    return true;
}

bool NetStatusService::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::INITIALIZING) {
        std::cerr << "NetStatusService not in initializing state" << std::endl;
        return false;
    }
    
    // 创建 vsomeip 应用
    app_ = vsomeip::runtime::get()->create_application("tbox-net-status");
    
    // 注册请求处理器
    app_->register_request_handler(
        service_ids::NET_STATUS,
        instance_ids::DEFAULT,
        method_ids::GET_NET_STATUS,
        std::bind(&NetStatusService::handleGetNetStatus, this, std::placeholders::_1)
    );
    
    // 初始化应用
    if (!app_->init()) {
        std::cerr << "Failed to initialize vsomeip application" << std::endl;
        return false;
    }
    
    // 启动应用
    app_->start();
    
    // 启动工作线程
    running_ = true;
    worker_thread_ = std::thread(&NetStatusService::run, this);
    
    state_ = ServiceState::AVAILABLE;
    std::cout << "NetStatusService started" << std::endl;
    return true;
}

void NetStatusService::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ == ServiceState::UNAVAILABLE) {
            return;
        }
        
        running_ = false;
    }
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (app_) {
        app_->stop();
        app_.reset();
    }
    
    if (tsp_client_) {
        tsp_client_->shutdown();
        tsp_client_.reset();
    }
    
    status_callbacks_.clear();
    state_ = ServiceState::UNAVAILABLE;
    
    std::cout << "NetStatusService stopped" << std::endl;
}

ServiceState NetStatusService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

uint16_t NetStatusService::getServiceId() const {
    return service_ids::NET_STATUS;
}

uint16_t NetStatusService::getInstanceId() const {
    return instance_ids::DEFAULT;
}

ErrorCode NetStatusService::getNetStatus(NetStatusData& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::AVAILABLE) {
        return ErrorCode::CLIENT_CALL_FAILED;
    }
    
    if (!tsp_client_ || !tsp_client_->isConnected()) {
        return ErrorCode::CLIENT_CALL_FAILED;
    }
    
    return tsp_client_->getNetStatus(status);
}

void NetStatusService::subscribeNetStatusChanges(NetStatusCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    status_callbacks_.push_back(callback);
    
    // 订阅网络状态变化
    if (tsp_client_) {
        tsp_client_->subscribeNetStatusChanges([this](const NetStatusData& status) {
            notifyNetStatusChange(status);
        });
    }
}

void NetStatusService::handleGetNetStatus(const std::shared_ptr<vsomeip::message>& request) {
    // 获取网络状态
    NetStatusData status;
    ErrorCode result = getNetStatus(status);
    
    // 创建响应
    auto response = vsomeip::runtime::get()->create_response(request);
    
    // 序列化响应
    // TODO: 实现实际的序列化逻辑
    
    // 发送响应
    app_->send(response);
}

void NetStatusService::notifyNetStatusChange(const NetStatusData& status) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& callback : status_callbacks_) {
        if (callback) {
            callback(status);
        }
    }
}

void NetStatusService::run() {
    while (running_) {
        // 定期检查网络状态
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

} // namespace someip
} // namespace tbox
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ./tests/unit/test_net_status`
预期：所有测试通过

- [ ] **步骤 5：Commit**

```bash
git add src/services/net_status.h src/services/net_status.cpp tests/unit/test_net_status.cpp
git commit -m "feat: implement NetStatus service for network status monitoring"
```

---

## 任务 8：RemoteCmdRelay 服务实现

**文件：**
- 创建：`src/services/remote_cmd_relay.h`
- 创建：`src/services/remote_cmd_relay.cpp`

- [ ] **步骤 1：编写失败的测试**

```cpp
// tests/unit/test_remote_cmd_relay.cpp
#include <gtest/gtest.h>
#include "services/remote_cmd_relay.h"
#include "ipc_bridge.h"

using namespace tbox::someip;

class RemoteCmdRelayTest : public ::testing::Test {
protected:
    void SetUp() override {
        bridge = std::make_unique<IpcBridge>();
        bridge->initialize();
        
        relay = std::make_unique<RemoteCmdRelayService>(bridge.get());
    }
    
    void TearDown() override {
        if (relay) {
            relay->stop();
        }
        if (bridge) {
            bridge->shutdown();
        }
    }
    
    std::unique_ptr<IpcBridge> bridge;
    std::unique_ptr<RemoteCmdRelayService> relay;
};

TEST_F(RemoteCmdRelayTest, InitializeSuccess) {
    EXPECT_TRUE(relay->initialize());
}

TEST_F(RemoteCmdRelayTest, SubscribeRemoteCommands) {
    EXPECT_TRUE(relay->initialize());
    EXPECT_TRUE(relay->start());
    
    bool received = false;
    relay->subscribeRemoteCommands([&received](const RemoteCommand&) {
        received = true;
    });
    
    // 订阅不应崩溃
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ./tests/unit/test_remote_cmd_relay`
预期：编译失败，报错 "RemoteCmdRelayService 类未定义"

- [ ] **步骤 3：编写最少实现代码**

```cpp
// src/services/remote_cmd_relay.h
#pragma once

#include "tbox_someip/interfaces.h"
#include <vsomeip/vsomeip.hpp>
#include <memory>
#include <mutex>
#include <atomic>
#include <thread>

namespace tbox {
namespace someip {

class IpcBridge;

class RemoteCmdRelayService : public ISomeipService {
public:
    RemoteCmdRelayService(IpcBridge* bridge);
    ~RemoteCmdRelayService() override;
    
    bool initialize() override;
    bool start() override;
    void stop() override;
    ServiceState getState() const override;
    uint16_t getServiceId() const override;
    uint16_t getInstanceId() const override;
    
    // RemoteCmdRelay 特定方法
    void subscribeRemoteCommands(RemoteCommandCallback callback);
    void notifyRemoteCommand(const RemoteCommand& command);

private:
    void run();
    void handleOnRemoteCommand(const std::shared_ptr<vsomeip::message>& request);
    
    IpcBridge* bridge_;
    ServiceState state_;
    std::shared_ptr<vsomeip::application> app_;
    std::shared_ptr<ITspClient> tsp_client_;
    
    std::vector<RemoteCommandCallback> command_callbacks_;
    mutable std::mutex mutex_;
    
    std::atomic<bool> running_;
    std::thread worker_thread_;
};

} // namespace someip
} // namespace tbox
```

```cpp
// src/services/remote_cmd_relay.cpp
#include "remote_cmd_relay.h"
#include "ipc_bridge.h"
#include <iostream>
#include <chrono>

namespace tbox {
namespace someip {

RemoteCmdRelayService::RemoteCmdRelayService(IpcBridge* bridge) 
    : bridge_(bridge)
    , state_(ServiceState::UNAVAILABLE)
    , running_(false) {}

RemoteCmdRelayService::~RemoteCmdRelayService() {
    stop();
}

bool RemoteCmdRelayService::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::UNAVAILABLE) {
        std::cerr << "RemoteCmdRelayService already initialized" << std::endl;
        return false;
    }
    
    if (!bridge_ || !bridge_->isInitialized()) {
        std::cerr << "IpcBridge not initialized" << std::endl;
        return false;
    }
    
    // 获取 TSP 客户端
    auto client = bridge_->getClient("tsp");
    if (!client) {
        std::cerr << "TSP client not found" << std::endl;
        return false;
    }
    
    tsp_client_ = std::dynamic_pointer_cast<ITspClient>(client);
    if (!tsp_client_) {
        std::cerr << "Invalid TSP client type" << std::endl;
        return false;
    }
    
    // 初始化 TSP 客户端
    if (!tsp_client_->initialize()) {
        std::cerr << "Failed to initialize TSP client" << std::endl;
        return false;
    }
    
    state_ = ServiceState::INITIALIZING;
    std::cout << "RemoteCmdRelayService initialized" << std::endl;
    return true;
}

bool RemoteCmdRelayService::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (state_ != ServiceState::INITIALIZING) {
        std::cerr << "RemoteCmdRelayService not in initializing state" << std::endl;
        return false;
    }
    
    // 创建 vsomeip 应用
    app_ = vsomeip::runtime::get()->create_application("tbox-remote-cmd-relay");
    
    // 注册事件处理器
    app_->register_message_handler(
        service_ids::REMOTE_CMD_RELAY,
        instance_ids::DEFAULT,
        event_ids::ON_REMOTE_COMMAND_NOTIFY,
        std::bind(&RemoteCmdRelayService::handleOnRemoteCommand, this, std::placeholders::_1)
    );
    
    // 初始化应用
    if (!app_->init()) {
        std::cerr << "Failed to initialize vsomeip application" << std::endl;
        return false;
    }
    
    // 启动应用
    app_->start();
    
    // 订阅远程命令
    if (tsp_client_) {
        tsp_client_->subscribeRemoteCommands([this](const RemoteCommand& command) {
            notifyRemoteCommand(command);
        });
    }
    
    // 启动工作线程
    running_ = true;
    worker_thread_ = std::thread(&RemoteCmdRelayService::run, this);
    
    state_ = ServiceState::AVAILABLE;
    std::cout << "RemoteCmdRelayService started" << std::endl;
    return true;
}

void RemoteCmdRelayService::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (state_ == ServiceState::UNAVAILABLE) {
            return;
        }
        
        running_ = false;
    }
    
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (app_) {
        app_->stop();
        app_.reset();
    }
    
    if (tsp_client_) {
        tsp_client_->shutdown();
        tsp_client_.reset();
    }
    
    command_callbacks_.clear();
    state_ = ServiceState::UNAVAILABLE;
    
    std::cout << "RemoteCmdRelayService stopped" << std::endl;
}

ServiceState RemoteCmdRelayService::getState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

uint16_t RemoteCmdRelayService::getServiceId() const {
    return service_ids::REMOTE_CMD_RELAY;
}

uint16_t RemoteCmdRelayService::getInstanceId() const {
    return instance_ids::DEFAULT;
}

void RemoteCmdRelayService::subscribeRemoteCommands(RemoteCommandCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    command_callbacks_.push_back(callback);
}

void RemoteCmdRelayService::notifyRemoteCommand(const RemoteCommand& command) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& callback : command_callbacks_) {
        if (callback) {
            callback(command);
        }
    }
}

void RemoteCmdRelayService::handleOnRemoteCommand(const std::shared_ptr<vsomeip::message>& request) {
    // 解析远程命令
    RemoteCommand command;
    // TODO: 实现实际的反序列化逻辑
    
    // 通知订阅者
    notifyRemoteCommand(command);
}

void RemoteCmdRelayService::run() {
    while (running_) {
        // 处理远程命令
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

} // namespace someip
} // namespace tbox
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ./tests/unit/test_remote_cmd_relay`
预期：所有测试通过

- [ ] **步骤 5：Commit**

```bash
git add src/services/remote_cmd_relay.h src/services/remote_cmd_relay.cpp tests/unit/test_remote_cmd_relay.cpp
git commit -m "feat: implement RemoteCmdRelay service for cloud command relay"
```

---

## 任务 9：主守护进程实现

**文件：**
- 创建：`src/someip_daemon.h`
- 创建：`src/someip_daemon.cpp`
- 创建：`src/main.cpp`

- [ ] **步骤 1：编写失败的测试**

```cpp
// tests/unit/test_someip_daemon.cpp
#include <gtest/gtest.h>
#include "someip_daemon.h"

using namespace tbox::someip;

class SomeipDaemonTest : public ::testing::Test {
protected:
    void SetUp() override {
        daemon = std::make_unique<SomeipDaemon>();
    }
    
    void TearDown() override {
        if (daemon) {
            daemon->shutdown();
        }
    }
    
    std::unique_ptr<SomeipDaemon> daemon;
};

TEST_F(SomeipDaemonTest, InitializeSuccess) {
    EXPECT_TRUE(daemon->initialize());
}

TEST_F(SomeipDaemonTest, StartWithoutInit) {
    EXPECT_FALSE(daemon->start());
}

TEST_F(SomeipDaemonTest, Shutdown) {
    EXPECT_TRUE(daemon->initialize());
    EXPECT_TRUE(daemon->start());
    
    // 关闭不应崩溃
    daemon->shutdown();
}
```

- [ ] **步骤 2：运行测试验证失败**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ./tests/unit/test_someip_daemon`
预期：编译失败，报错 "SomeipDaemon 类未定义"

- [ ] **步骤 3：编写最少实现代码**

```cpp
// src/someip_daemon.h
#pragma once

#include "tbox_someip/config.h"
#include "ipc_bridge.h"
#include "service_discovery.h"
#include "services/time_sync.h"
#include "services/location.h"
#include "services/net_status.h"
#include "services/remote_cmd_relay.h"

#include <memory>
#include <atomic>
#include <thread>
#include <signal.h>

namespace tbox {
namespace someip {

class SomeipDaemon {
public:
    SomeipDaemon();
    ~SomeipDaemon();
    
    // 初始化守护进程
    bool initialize(const ServiceConfig& config = getDefaultConfig());
    
    // 启动守护进程
    bool start();
    
    // 关闭守护进程
    void shutdown();
    
    // 获取运行状态
    bool isRunning() const;

private:
    // 信号处理
    static void signalHandler(int signum);
    
    // 初始化 IPC 客户端
    bool initializeIpcClients();
    
    // 初始化服务
    bool initializeServices();
    
    // 启动服务发现
    bool startServiceDiscovery();
    
    ServiceConfig config_;
    
    std::unique_ptr<IpcBridge> ipc_bridge_;
    std::unique_ptr<ServiceDiscovery> service_discovery_;
    
    std::unique_ptr<TimeSyncService> time_sync_service_;
    std::unique_ptr<LocationService> location_service_;
    std::unique_ptr<NetStatusService> net_status_service_;
    std::unique_ptr<RemoteCmdRelayService> remote_cmd_relay_service_;
    
    std::atomic<bool> running_;
    static SomeipDaemon* instance_;
};

} // namespace someip
} // namespace tbox
```

```cpp
// src/someip_daemon.cpp
#include "someip_daemon.h"
#include <iostream>
#include <csignal>

namespace tbox {
namespace someip {

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
    // TODO: 实现实际的 IPC 客户端初始化
    // 这里需要创建 GNSS、RTC、TSP、PROV 客户端并注册到 IPC 桥接层
    
    std::cout << "Initializing IPC clients..." << std::endl;
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
    
    std::cout << "All services initialized" << std::endl;
    return true;
}

bool SomeipDaemon::startServiceDiscovery() {
    if (!service_discovery_) {
        return false;
    }
    
    std::cout << "Starting service discovery..." << std::endl;
    
    // 发布所有服务
    if (time_sync_service_ && time_sync_service_->getState() == ServiceState::INITIALIZING) {
        if (!service_discovery_->offerService(
            time_sync_service_->getServiceId(),
            time_sync_service_->getInstanceId())) {
            std::cerr << "Failed to offer TimeSync service" << std::endl;
            return false;
        }
    }
    
    if (location_service_ && location_service_->getState() == ServiceState::INITIALIZING) {
        if (!service_discovery_->offerService(
            location_service_->getServiceId(),
            location_service_->getInstanceId())) {
            std::cerr << "Failed to offer Location service" << std::endl;
            return false;
        }
    }
    
    if (net_status_service_ && net_status_service_->getState() == ServiceState::INITIALIZING) {
        if (!service_discovery_->offerService(
            net_status_service_->getServiceId(),
            net_status_service_->getInstanceId())) {
            std::cerr << "Failed to offer NetStatus service" << std::endl;
            return false;
        }
    }
    
    if (remote_cmd_relay_service_ && remote_cmd_relay_service_->getState() == ServiceState::INITIALIZING) {
        if (!service_discovery_->offerService(
            remote_cmd_relay_service_->getServiceId(),
            remote_cmd_relay_service_->getInstanceId())) {
            std::cerr << "Failed to offer RemoteCmdRelay service" << std::endl;
            return false;
        }
    }
    
    std::cout << "Service discovery started" << std::endl;
    return true;
}

} // namespace someip
} // namespace tbox
```

```cpp
// src/main.cpp
#include "someip_daemon.h"
#include <iostream>
#include <unistd.h>

int main(int argc, char* argv[]) {
    std::cout << "TBOX-SOMEIP Service v1.0.0" << std::endl;
    
    tbox::someip::SomeipDaemon daemon;
    
    // 初始化守护进程
    if (!daemon.initialize()) {
        std::cerr << "Failed to initialize daemon" << std::endl;
        return 1;
    }
    
    // 启动守护进程
    if (!daemon.start()) {
        std::cerr << "Failed to start daemon" << std::endl;
        return 1;
    }
    
    std::cout << "Daemon running. Press Ctrl+C to stop." << std::endl;
    
    // 主循环
    while (daemon.isRunning()) {
        sleep(1);
    }
    
    return 0;
}
```

- [ ] **步骤 4：运行测试验证通过**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ./tests/unit/test_someip_daemon`
预期：所有测试通过

- [ ] **步骤 5：Commit**

```bash
git add src/someip_daemon.h src/someip_daemon.cpp src/main.cpp tests/unit/test_someip_daemon.cpp
git commit -m "feat: implement SomeipDaemon as main service entry point"
```

---

## 任务 10：systemd 服务配置

**文件：**
- 创建：`systemd/tbox-someip.service`
- 创建：`config/someip.json`

- [ ] **步骤 1：创建 systemd 服务文件**

```ini
# systemd/tbox-someip.service
[Unit]
Description=TBOX-SOMEIP Service
After=network-online.target
Wants=network-online.target
After=gnss.service rtc.service tsp.service prov.service
Requires=network-online.target

[Service]
Type=simple
ExecStart=/usr/bin/tbox-someip
Restart=on-failure
RestartSec=5
WatchdogSec=30

# 资源限制
LimitNOFILE=65536
LimitNPROC=4096

# 安全限制
User=tbox
Group=tbox
PrivateTmp=true
ProtectSystem=strict
ProtectHome=true
NoNewPrivileges=true

# 日志
StandardOutput=journal
StandardError=journal
SyslogIdentifier=tbox-someip

[Install]
WantedBy=multi-user.target
```

- [ ] **步骤 2：创建 vsomeip 配置文件**

```json
{
    "applications": [
        {
            "name": "tbox-someip",
            "id": "0x1234"
        }
    ],
    "services": [
        {
            "service": "0x0100",
            "instance": "0x0001",
            "unreliable": "30509"
        },
        {
            "service": "0x0101",
            "instance": "0x0001",
            "unreliable": "30510"
        },
        {
            "service": "0x0102",
            "instance": "0x0001",
            "unreliable": "30511"
        },
        {
            "service": "0x0103",
            "instance": "0x0001",
            "unreliable": "30512"
        }
    ],
    "routing": "tbox-someip",
    "service-discovery": {
        "enable": "true",
        "multicast": "239.0.0.1",
        "port": "30490",
        "protocol": "udp",
        "initial_delay_min": "10",
        "initial_delay_max": "100",
        "repetitions_base_delay": "200",
        "repetitions_max": "3",
        "cyclic_offer_delay": "1000",
        "request_response_delay": "1500"
    },
    "network": {
        "primary": {
            "name": "eth0"
        }
    },
    "diagnosis": {
        "address": "0x00",
        "supplier": "0x0000",
        "function": "0x0000",
        "variant": "0x00"
    },
    "payload": {
        "max-serialized-size": "65535"
    },
    "threads": {
        "request": "4",
        "io": "2"
    }
}
```

- [ ] **步骤 3：验证配置文件语法**

运行：`python3 -m json.tool config/someip.json`
预期：JSON 格式正确，无语法错误

- [ ] **步骤 4：Commit**

```bash
git add systemd/tbox-someip.service config/someip.json
git commit -m "feat: add systemd service configuration and vsomeip config"
```

---

## 任务 11：集成测试

**文件：**
- 创建：`tests/integration/test_someip_integration.cpp`

- [ ] **步骤 1：编写集成测试**

```cpp
// tests/integration/test_someip_integration.cpp
#include <gtest/gtest.h>
#include "someip_daemon.h"
#include <thread>
#include <chrono>

using namespace tbox::someip;

class SomeipIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        daemon = std::make_unique<SomeipDaemon>();
    }
    
    void TearDown() override {
        if (daemon) {
            daemon->shutdown();
        }
    }
    
    std::unique_ptr<SomeipDaemon> daemon;
};

TEST_F(SomeipIntegrationTest, FullServiceLifecycle) {
    // 测试完整的服务生命周期
    
    // 1. 初始化
    ServiceConfig config;
    config.enable_time_sync = true;
    config.enable_location = true;
    config.enable_net_status = true;
    config.enable_remote_cmd_relay = true;
    
    EXPECT_TRUE(daemon->initialize(config));
    
    // 2. 启动
    EXPECT_TRUE(daemon->start());
    EXPECT_TRUE(daemon->isRunning());
    
    // 3. 运行一段时间
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // 4. 关闭
    daemon->shutdown();
    EXPECT_FALSE(daemon->isRunning());
}

TEST_F(SomeipIntegrationTest, PartialServiceEnable) {
    // 测试部分服务启用
    
    ServiceConfig config;
    config.enable_time_sync = true;
    config.enable_location = false;
    config.enable_net_status = true;
    config.enable_remote_cmd_relay = false;
    
    EXPECT_TRUE(daemon->initialize(config));
    EXPECT_TRUE(daemon->start());
    
    std::this_thread::sleep_for(std::chrono::seconds(1));
    
    daemon->shutdown();
}

TEST_F(SomeipIntegrationTest, ServiceDiscoveryIntegration) {
    // 测试服务发现集成
    
    ServiceConfig config;
    config.enable_sd = true;
    config.sd_offer_interval_ms = 500;
    
    EXPECT_TRUE(daemon->initialize(config));
    EXPECT_TRUE(daemon->start());
    
    // 等待服务发现完成
    std::this_thread::sleep_for(std::chrono::seconds(3));
    
    daemon->shutdown();
}

TEST_F(SomeipIntegrationTest, MultipleStartStop) {
    // 测试多次启停
    
    ServiceConfig config;
    
    EXPECT_TRUE(daemon->initialize(config));
    
    for (int i = 0; i < 3; i++) {
        EXPECT_TRUE(daemon->start());
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        daemon->shutdown();
    }
}
```

- [ ] **步骤 2：运行集成测试**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ./tests/integration/test_someip_integration`
预期：所有集成测试通过

- [ ] **步骤 3：Commit**

```bash
git add tests/integration/test_someip_integration.cpp
git commit -m "test: add integration tests for full service lifecycle"
```

---

## 任务 12：文档与最终验证

**文件：**
- 创建：`docs/api.md`
- 创建：`README.md`（更新）

- [ ] **步骤 1：创建 API 文档**

```markdown
# TBOX-SOMEIP API 文档

## 概述

TBOX-SOMEIP 是 TBOX 面向车内 SOME/IP 网络的唯一接入/适配门面服务。

## 服务列表

### 1. TimeSync（时间同步）
- **Service ID**: 0x0100
- **Instance ID**: 0x0001
- **类型**: Field/Event

#### 接口
- `getTime()`: 获取当前时间
- `onTimeUpdate`: 时间更新事件

### 2. Location（定位）
- **Service ID**: 0x0101
- **Instance ID**: 0x0001
- **类型**: Event/Field

#### 接口
- `getPosition()`: 获取当前位置
- `onPosition`: 位置更新事件

### 3. NetStatus（网络状态）
- **Service ID**: 0x0102
- **Instance ID**: 0x0001
- **类型**: Field/Event

#### 接口
- `getNetStatus()`: 获取网络状态
- `onNetStatusChange`: 网络状态变化事件

### 4. RemoteCmdRelay（云指令中继）
- **Service ID**: 0x0103
- **Instance ID**: 0x0001
- **类型**: Method/Event

#### 接口
- `onRemoteCommand`: 远程命令事件

## 错误码

| 错误码 | 说明 |
|--------|------|
| 0 | 成功 |
| 1001 | 网络/端口未就绪，SD 发布失败 |
| 1002 | 内部 client 调用超时 |
| 1003 | 内部 client 调用失败/不可用 |
| 1004 | 序列化/映射转换失败 |

## 配置

配置文件位于 `/etc/tbox-someip/someip.json`。

### 主要配置项
- `network.primary.name`: 主网络接口
- `service-discovery.enable`: 启用服务发现
- `service-discovery.multicast`: 多播地址
- `service-discovery.port`: SD 端口

## systemd 服务

服务文件位于 `/etc/systemd/system/tbox-someip.service`。

### 启动命令
```bash
sudo systemctl start tbox-someip
sudo systemctl enable tbox-someip
```

### 日志查看
```bash
journalctl -u tbox-someip -f
```
```

- [ ] **步骤 2：更新 README.md**

```markdown
# TBOX-SOMEIP Service

TBOX 面向车内 SOME/IP 网络的唯一接入/适配门面服务。

## 功能特性

- 常驻服务（daemon）架构
- 对内通过 IPC client 桩访问基础服务
- 对外通过 vsomeip/CommonAPI 暴露 SOME/IP 服务
- 支持 SOME/IP-SD 服务发现
- 生命周期与编排管理
- 错误处理与可观测性

## 依赖

- C++17 编译器
- CMake 3.16+
- vsomeip 3.x
- CommonAPI
- systemd

## 构建

```bash
mkdir build && cd build
cmake ..
make
```

## 测试

```bash
cd build
cmake .. -DBUILD_TESTS=ON
make
ctest
```

## 安装

```bash
sudo make install
sudo systemctl daemon-reload
sudo systemctl enable tbox-someip
sudo systemctl start tbox-someip
```

## 配置

配置文件位于 `config/someip.json`。

## 文档

- [API 文档](docs/api.md)
- [设计文档](https://app.notion.com/p/3a50ec7a4b9280d699bed6735f7ffa34)

## 许可证

Apache License 2.0
```

- [ ] **步骤 3：运行完整测试套件**

运行：`cd build && cmake .. -DBUILD_TESTS=ON && make && ctest --output-on-failure`
预期：所有测试通过

- [ ] **步骤 4：最终 Commit**

```bash
git add docs/api.md README.md
git commit -m "docs: add API documentation and update README"
```

---

## 执行交接

**计划已完成并保存到 `docs/superpowers/plans/2026-07-22-tbox-someip-cr-001.md`。两种执行方式：**

**1. 子代理驱动（推荐）** - 每个任务调度一个新的子代理，任务间进行审查，快速迭代

**2. 内联执行** - 在当前会话中使用 executing-plans 执行任务，批量执行并设有检查点

**选哪种方式？**
