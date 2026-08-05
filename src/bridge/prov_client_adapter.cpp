// ProvClientAdapter 实现 (CR-006)
//
// 封装 tbox::prov::ProvClient facade。

#include "prov_client_adapter.h"

#include "tbox/prov/errors.h"
#include "tbox/prov/types.h"

namespace tbox {
namespace someip {

namespace {

std::string provisionStateToString(tbox::prov::ProvisionState state) {
    switch (state) {
        case tbox::prov::ProvisionState::NONE:        return "none";
        case tbox::prov::ProvisionState::VIN_WRITTEN: return "vin_written";
        case tbox::prov::ProvisionState::BOUND:       return "bound";
        case tbox::prov::ProvisionState::FAILED:      return "failed";
    }
    return "unknown";
}

} // anonymous namespace

// ============================================================
// 构造/析构
// ============================================================

ProvClientAdapter::ProvClientAdapter(const ProvAdapterConfig& config)
    : config_(config)
    , prov_client_(std::make_unique<tbox::prov::ProvClient>(config.socket_path)) {
}

ProvClientAdapter::~ProvClientAdapter() {
    shutdown();
}

// ============================================================
// IIpcClient
// ============================================================

bool ProvClientAdapter::initialize() {
    if (!prov_client_) {
        return false;
    }
    bool ok = prov_client_->connect();
    SomeipLogAdapter::ipc_bridge().info("someip.ipc.prov.connect",
        "PROV client adapter initialize",
        {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("prov")),
         tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("initialize")),
         tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString(ok ? "succeeded" : "failed"))}
    );
    return ok;
}

bool ProvClientAdapter::isConnected() const {
    return prov_client_ && prov_client_->is_connected();
}

void ProvClientAdapter::shutdown() {
    if (prov_client_) {
        prov_client_->disconnect();
    }
    SomeipLogAdapter::ipc_bridge().info("someip.ipc.prov.shutdown",
        "PROV client adapter shutdown"
    );
}

// ============================================================
// IProvClient
// ============================================================

std::string ProvClientAdapter::getConfiguration(const std::string& key) {
    if (!prov_client_ || !prov_client_->is_connected()) {
        SomeipLogAdapter::ipc_bridge().error("someip.ipc.call.failed",
            "PROV client not connected",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("prov")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("get_configuration")),
             tbox::fw::log::Field("key", tbox::fw::log::FieldValue::makeString(key)),
             tbox::fw::log::Field("result", tbox::fw::log::FieldValue::makeString("failed")),
             tbox::fw::log::Field("error_code", tbox::fw::log::FieldValue::makeString("TBOX-SOMEIP-1003"))}
        );
        return "";
    }

    // 映射到 PROV facade 只读接口。不记录返回值明文 (敏感数据脱敏)。
    std::string result;
    if (key == "vin") {
        result = prov_client_->read_vin();
    } else if (key == "provision_state") {
        result = provisionStateToString(prov_client_->get_provision_state());
    } else if (key == "sn") {
        result = prov_client_->read_binding().sn;
    } else if (key == "ecu_uid") {
        result = prov_client_->read_binding().ecu_uid;
    } else {
        SomeipLogAdapter::ipc_bridge().warn("someip.ipc.prov.unknown_key",
            "Unknown PROV configuration key",
            {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("prov")),
             tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("get_configuration")),
             tbox::fw::log::Field("key", tbox::fw::log::FieldValue::makeString(key))}
        );
        return "";
    }

    SomeipLogAdapter::ipc_bridge().info("someip.ipc.prov.get",
        "PROV configuration read via prov_client",
        {tbox::fw::log::Field("internal_service", tbox::fw::log::FieldValue::makeString("prov")),
         tbox::fw::log::Field("operation", tbox::fw::log::FieldValue::makeString("get_configuration")),
         tbox::fw::log::Field("key", tbox::fw::log::FieldValue::makeString(key)),
         tbox::fw::log::Field("found", tbox::fw::log::FieldValue::makeBool(!result.empty()))}
    );
    return result;
}

} // namespace someip
} // namespace tbox
