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
