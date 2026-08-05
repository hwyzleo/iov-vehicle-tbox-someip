#pragma once

// ProvClientAdapter (CR-006)
//
// 通过 tbox::prov::ProvClient
// (framework-ipc 域 facade) 访问 PROV daemon。
//
// 边界：
// - 不直接创建 framework Client、不解析 provider wire。
// - IProvClient::getConfiguration(key) 映射到 PROV facade 的只读接口
//   （vin / provision_state / sn / ecu_uid）；未知 key 返回空串。
// - 不记录 VIN / SN / ECU UID 等敏感值明文（脱敏由 facade/Logger 负责）。

#include "tbox_someip/interfaces.h"
#include "tbox_someip/types.h"
#include "tbox_someip/someip_log_adapter.h"

#include "tbox/prov/client.h"   // tbox::prov::ProvClient

#include <memory>
#include <string>

namespace tbox {
namespace someip {

/// PROV 适配器配置
struct ProvAdapterConfig {
    std::string socket_path = "/tmp/tbox-prov.sock";
};

class ProvClientAdapter : public IProvClient {
public:
    explicit ProvClientAdapter(const ProvAdapterConfig& config = ProvAdapterConfig{});
    ~ProvClientAdapter() override;

    ProvClientAdapter(const ProvClientAdapter&) = delete;
    ProvClientAdapter& operator=(const ProvClientAdapter&) = delete;

    // ---- IIpcClient ----
    bool initialize() override;
    bool isConnected() const override;
    void shutdown() override;

    // ---- IProvClient ----
    // 支持的 key：vin / provision_state / sn / ecu_uid（未知 key 返回 ""）
    std::string getConfiguration(const std::string& key) override;

private:
    ProvAdapterConfig config_;
    std::unique_ptr<tbox::prov::ProvClient> prov_client_;
};

} // namespace someip
} // namespace tbox
