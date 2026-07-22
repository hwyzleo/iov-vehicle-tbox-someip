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
