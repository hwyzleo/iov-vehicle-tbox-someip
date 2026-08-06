# TBOX-SOMEIP 接入 TBOX-BUILD 说明

> 对应设计变更：TBOX-SOMEIP-DSN-CR-006（§11 Orin 统一构建设计）
> 对应需求：TBOX-SOMEIP-REQ-CR-006（US-009 / SOMEIP-BUILD-REQ-001~011）

本文档描述 SOMEIP 仓库侧的构建接入事实（target / unit / config / 依赖 /
验证入口），供 TBOX-BUILD 的 `manifests/services.yaml` metadata 与
`dependencies/lock.yaml` 引用。字段/路径以本仓库事实为准。

## 1. 责任边界

| 事项 | SOMEIP 仓库 | BUILD 仓库 |
|---|---|---|
| SOME/IP ID、wire、桥接语义 | 定义并维护 | 不复制、不改变 |
| CMake target/install | 定义并验证（本仓库） | 通过统一入口调用 |
| vsomeip/CommonAPI | 声明接口和版本要求 | 提供受控 HOST/TARGET 输入 |
| TBOX client SDK 消费 | 使用 installed packages | 冻结 SDK/release-set |
| systemd 与非秘密配置 | 定义、安装并验证 | 收集、引用和打包 |
| health/SOME-IP smoke | 实现（tests/smoke/） | 受控执行并归档结果 |
| CI/toolchain/sysroot/staging | 使用，不复制 | 定义并维护 |

SOMEIP 不维护独立 Orin toolchain、sysroot、容器或发布入口；正式整体构建
入口为 BUILD 仓库根目录执行 `./ci/build-in-docker.sh`。

## 2. 构建事实（实际 target / unit / config）

| 项 | 事实值 |
|---|---|
| CMake project | `tbox-someip`，C++17，GNUInstallDirs |
| daemon target | `tbox-someip`（`add_executable`，src/CMakeLists.txt） |
| install component | `someip-runtime`（staging: rootfs，逻辑 prefix /usr） |
| daemon 安装路径 | `${CMAKE_INSTALL_BINDIR}/tbox-someip`（→ /usr/bin/tbox-someip） |
| systemd unit | `systemd/tbox-someip.service` → `${CMAKE_INSTALL_LIBDIR}/systemd/system` |
| daemon 配置 | `config/someip.default.yaml` → `${CMAKE_INSTALL_SYSCONFDIR}/tbox/conf.d/someip.yaml` |
| vsomeip 配置 | `config/runtime/vsomeip.json` → `${CMAKE_INSTALL_SYSCONFDIR}/tbox/someip/vsomeip.json` |
| health check | `tests/smoke/someip-health.sh` |
| smoke test | `tests/smoke/someip-sd-smoke.sh` |

### 2.1 systemd 启动语义（CR-006 §7/§11.5）

```ini
After=network-online.target tsp.service prov.service
Wants=network-online.target
```

- `After=network-online.target`：SOME/IP-SD Offer 需网络就绪；用 `Wants` 而非
  `Requires`（网络晚到 → 延迟 Offer/暂不可用，不阻塞启动）。
- `After=tsp.service prov.service`：SOMEIP 作为 consumer 连接 TSP/PROV 的
  IPC socket（唯一对内接入方式）；依赖未就绪时保持延迟 Offer/按需重连语义。
- 构建依赖（framework 无 unit）不得机械转成 systemd Requires=。

## 3. TBOX SDK 消费（installed packages only）

```cmake
find_package(TboxFramework CONFIG REQUIRED)   # TBoxFramework::config/log/ipc
find_package(TboxTspClient CONFIG REQUIRED)   # tbox::tsp_client（始终链接）
find_package(TboxProvClient CONFIG REQUIRED)  # tbox::prov_client（始终链接）
```

- 禁止 `add_subdirectory`、上游私有头文件、build-tree library、宿主绝对路径
  或复制 client transport。
- `TboxFramework` 组件按 daemon 实际使用声明：`config`（hwyz::config）、
  `log`（tbox::fw::log）、`ipc`（tbox::fw::ipc Subscription，TSP adapter）。
- MQTT/SEC 由 TSP/MQTT 构建闭包进入 release-set，SOMEIP 无直接代码使用，
  不冗余声明为直接依赖。GNSS/RTC 在对应 client SDK 就绪并被代码实际使用
  后加入。
- PROV client 当前仅构建接入（可解析/可链接，满足 REQ-004/REQ-010）；
  代码层 prov 业务链路（只读/通知）按设计 §10.4 分步迁移，在能力定版
  CR 实现，本期不虚构业务代码。

## 4. SOME/IP 中间件 HOST/TARGET 分离（CR-006 §11.4）

| 类别 | 依赖 | 来源 |
|---|---|---|
| HOST | CommonAPI core/SOME-IP generator 及代码生成依赖 | 仅受控 HOST 执行，只产生构建中间文件 |
| TARGET | vsomeip、CommonAPI runtime（及实际 link 依赖，如 Boost） | Orin sysroot 或受控 recipe（BUILD `TBOX_DEP_STAGING`） |

- SOMEIP CMake 通过 `pkg_check_modules`（`vsomeip3` / `CommonAPI` /
  `CommonAPI-SomeIP`）查找 TARGET runtime；`PKG_CONFIG_USE_CMAKE_PREFIX_PATH=ON`
  使其跟随 BUILD 注入的 `CMAKE_PREFIX_PATH`（`<TBOX_DEP_STAGING>/usr`）。
- BUILD toolchain 已保证 `CMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER`（HOST
  generator 不得从 TARGET sysroot 执行）、`LIBRARY/INCLUDE/PACKAGE=ONLY`
  （TARGET linker 不得解析 HOST 库）。
- 默认 `USE_MOCK_SOMEIP=ON` 用于本地开发/单测（不依赖 vsomeip）；Orin
  正式构建由 BUILD 注入 `USE_MOCK_SOMEIP=OFF`。

## 5. 构建开关

| 选项 | 默认 | 说明 |
|---|---|---|---|
| `USE_MOCK_SOMEIP` | OFF | mock SOME/IP（本地开发/单测，scripts/build.sh 显式 ON）；OFF = 真实 vsomeip/CommonAPI |
| `BUILD_TESTS` | OFF | 测试构建（BUILD 模式不构建 GTest；本地 build.sh 显式 ON） |

对内 TSP/PROV 始终经 framework-ipc 域 client facade（tbox::tsp_client +
tbox::prov_client）接入真实后端，无 mock IPC 桩，无构建开关。

Orin 正式构建由 BUILD 经 `manifests/services.yaml` 的 `build.cmake_cache_variables`
注入 `USE_MOCK_SOMEIP=OFF`（BUILD 通用机制，受控输入，
同 toolchain/sysroot 注入）：

```yaml
build:
  cmake_cache_variables:
    USE_MOCK_SOMEIP: "OFF"
```

## 6. someip-runtime 安装内容（staging: rootfs）

```
out/<platform>/<profile>/install-root/
├── usr/bin/tbox-someip
├── usr/lib/systemd/system/tbox-someip.service
└── etc/tbox/conf.d/someip.yaml
    etc/tbox/someip/vsomeip.json
```

rootfs **不安装** HOST generator、测试 peer、fixture、源码、开发头文件、
静态库或 CMake package（无独立 SDK 交付需求，不创建 someip_client）。

## 7. 验证入口

- `tests/smoke/someip-health.sh`：systemd active + 唯一实例。
- `tests/smoke/someip-sd-smoke.sh`：配置存在性/敏感字段检查、重启 + 唯一
  实例、TCP 56101 端点（Service 0x6101 / Instance 0x0001）、SD UDP 30490 探测。
- `scripts/test_someip_connection.py`：SOME/IP TCP 56101 最小连接/请求。
- `scripts/verify-elf.sh`：ELF64/AArch64、NEEDED、RPATH/RUNPATH、宿主污染
  自检（`--strict` 用于 Orin 制品；正式校验归 BUILD elfcheck）。

## 8. 协议常量（非秘密配置，可保留）

Service ID `0x6101`、Instance ID `0x0001`、TCP `56101`、SD UDP `30490`、
Method `0x0001`（REPORT_SOFTWARE_INVENTORY）、Event `0x8001`
（FOTA_COMMAND）为非秘密协议常量；网卡、unicast/multicast 地址与环境路由
通过受控配置注入（config/runtime/vsomeip.json `network.primary.name` 等为默认占位）。

## 9. 配置分层（CR-007）

| 层 | 路径 | 用途 | 安装 |
|---|---|---|---|
| 本地调试 | `config/dev/common.yaml` + `config/dev/someip.yaml` | 开发/隔离 peer/单测 | 不安装 |
| 量产默认 | `config/someip.default.yaml` | 服务默认模板 | someip-runtime -> /etc/tbox/conf.d/someip.yaml（RENAME） |
| Schema | `config/schema/someip.schema.yaml` | strict 校验 | 不安装（CI 用） |
| vsomeip runtime | `config/runtime/vsomeip.json` | vsomeip runtime 强制配置 | someip-runtime -> /etc/tbox/someip/vsomeip.json |
| BUILD overlay | `configs/orin/rootfs/etc/tbox/conf.d/someip.yaml`（BUILD 仓库） | 可选覆盖 | overlay after install |

runtime 加载顺序：`/etc/tbox/common.yaml` -> `/etc/tbox/conf.d/someip.yaml`。
`config/dev/**` 不由 someip-runtime 安装，不进入 production rootfs/package。

### 9.1 寻址 SSOT（方案 C）
- 寻址（service/instance/port/SD/method/event）运行时 SSOT 为 `someip.yaml`，代码从 config 读取。
- `types.h` 常量为 Registry 基线默认值 + CI 一致性校验锚点，config 缺失字段时 fallback。
- `vsomeip.json` 寻址字段必须与 `someip.yaml` 一致（CI gate D 校验），不成为第二套无校验 SSOT。

### 9.2 部署策略
```
/etc/tbox/common.yaml           = release-managed + replace
/etc/tbox/conf.d/someip.yaml    = release-managed + replace
/etc/tbox/someip/vsomeip.json   = release-managed + replace
<device-network-provisioning>   = device-managed + preserve
```

### 9.3 CI 门禁
`scripts/validate_config.py` + `tests/test_config_validation.sh` 实现 gate A–E：
schema/未知键/有界/ID 一致性/禁止内容扫描/dev 差集。
