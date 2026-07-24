# SOME/IP Mock 模式指南

## 概述

在 macOS 开发环境中，由于无法直接使用 vsomeip 库，我们实现了 mock 模式下的 SOME/IP 基础功能。这个实现参考了 `iov-vehicle-cgw-diag` 项目，提供了完整的 SOME/IP 协议栈模拟。

## 功能特性

### 1. TCP 端口监听
- 监听指定端口（默认 56101）
- 支持客户端连接和断开
- 单连接模型（简化实现）

### 2. SOME/IP 协议处理
- 标准 SOME/IP 头部序列化/反序列化
- 请求/响应消息处理
- 消息类型和返回码支持

### 3. 服务管理
- 服务注册和注销
- 方法处理器注册
- 事件处理器注册

## 架构设计

```
┌─────────────────────────────────────────────────────────┐
│                    SomeipDaemon                         │
├─────────────────────────────────────────────────────────┤
│  ServiceDiscovery ──── SomeIpService (Mock)            │
│         │                     │                         │
│         ▼                     ▼                         │
│  ┌─────────────┐    ┌─────────────────┐                │
│  │ 回调管理     │    │ TCP Socket      │                │
│  │ 服务发现     │    │ 监听 56101      │                │
│  └─────────────┘    └─────────────────┘                │
└─────────────────────────────────────────────────────────┘
```

## 文件结构

```
include/tbox_someip/
├── someip_types.h          # SOME/IP 协议类型定义
├── someip_service.h        # SOME/IP 服务接口
└── interfaces.h            # IPC 客户端接口

src/
├── someip_service.cpp      # Mock SOME/IP 服务实现
├── service_discovery.cpp   # 服务发现实现
└── someip_daemon.cpp       # 守护进程主逻辑
```

## 使用方法

### 1. 编译项目

```bash
mkdir build && cd build
cmake ..
make -j4
```

### 2. 运行服务

```bash
./tbox-someip
```

你应该看到类似以下的输出：
```
TBOX-SOMEIP Service v1.0.0
Initializing TBOX-SOMEIP daemon...
...
Offering service: 0x6101 instance: 0x1 on port 56101
[INFO] MockSomeIP listening on 0.0.0.0:56101 for service=0x6101 instance=0x1
...
```

### 3. 测试连接

使用提供的测试脚本：

```bash
python3 scripts/test_someip_connection.py
```

或者使用 netcat：

```bash
nc -zv localhost 56101
```

## SOME/IP 消息格式

### 请求消息
```
+--------+--------+--------+--------+--------+--------+--------+--------+
| Service ID (16) | Method ID (16) | Length (32)                        |
+--------+--------+--------+--------+--------+--------+--------+--------+
| Client ID (16)  | Session ID (16)| ProtoVer | IfaceVer | MsgType | RetCode |
+--------+--------+--------+--------+--------+--------+--------+--------+
| Payload (variable length)                                              |
+--------+--------+--------+--------+--------+--------+--------+--------+
```

### 响应消息
- Message Type: 0x80 (RESPONSE)
- Return Code: 0x00 (E_OK) 或其他错误码

## 配置选项

在 `include/tbox_someip/config.h` 中可以配置：

```cpp
struct ServiceConfig {
    std::string network_interface = "eth0";
    uint16_t sd_port = 30490;           // 服务发现端口
    bool enable_sd = true;              // 启用服务发现
    // ... 其他配置
};
```

## 扩展点

### 1. 添加新的方法处理器

在 `SomeipDaemon::startServiceDiscovery()` 中注册：

```cpp
someip_service_->registerMethodHandler(0x0002, [](const SomeIpMessage& request) {
    SomeIpMessage response;
    response.header.service_id = request.header.service_id;
    response.header.method_id = request.header.method_id;
    response.header.message_type = SomeIpMessageType::RESPONSE;
    response.header.return_code = SomeIpReturnCode::E_OK;
    // 处理请求并设置响应负载
    return response;
});
```

### 2. 添加事件通知

```cpp
someip_service_->registerEventHandler(0x8001, [](const SomeIpMessage& notification) {
    // 处理事件通知
});
```

## 测试

### 单元测试

```bash
cd build
./tests/test_fota_relay
./tests/test_someip_integration
```

### 集成测试

集成测试会验证完整的服务生命周期，包括：
- 服务初始化
- 端口监听
- 客户端连接
- 请求/响应处理
- 服务关闭

## 注意事项

1. **Mock 模式限制**：当前实现仅用于开发和测试，不支持真实的 SOME/IP 协议栈功能。

2. **单连接模型**：每个服务实例只支持一个客户端连接，多客户端连接会被拒绝。

3. **端口冲突**：如果端口 56101 已被占用，服务将无法启动。

4. **线程安全**：所有操作都是线程安全的，使用了互斥锁保护共享数据。

## 下一步

1. **实现 vsomeip 集成**：在 Linux 环境中集成真实的 vsomeip 库。

2. **多客户端支持**：扩展为支持多个并发客户端连接。

3. **服务发现增强**：实现完整的服务发现协议。

4. **性能优化**：添加连接池和消息队列。

## 故障排除

### 端口被占用

```bash
lsof -i :56101
# 找到占用端口的进程并终止
```

### 连接被拒绝

确保服务正在运行：
```bash
ps aux | grep tbox-someip
```

### 编译错误

确保所有依赖已安装：
```bash
# macOS
brew install cmake googletest
```

## 参考资料

- [SOME/IP 协议规范](https://www.autosar.org/fileadmin/standards/R22-11/CP/AUTOSAR_SWS_SOMEIPProtocol.pdf)
- [vsomeip 文档](https://github.com/COVESA/vsomeip)
- [iov-vehicle-cgw-diag 项目](../cgw/iov-vehicle-cgw-diag/)