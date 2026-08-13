#include "tbox_someip/someip_log_adapter.h"

namespace tbox {
namespace someip {

bool SomeipLogAdapter::s_initialized = false;

tbox::fw::log::InitResult SomeipLogAdapter::init(
    const std::string& service,
    const tbox::fw::log::LogConfig& config
) {
    auto result = tbox::fw::log::Logger::init(service, config);
    if (result.error == tbox::fw::log::LogError::kOk) {
        s_initialized = true;
    }
    return result;
}

tbox::fw::log::Logger SomeipLogAdapter::runtime() {
    return tbox::fw::log::Logger::get("runtime");
}

tbox::fw::log::Logger SomeipLogAdapter::sd() {
    return tbox::fw::log::Logger::get("sd");
}

tbox::fw::log::Logger SomeipLogAdapter::method() {
    return tbox::fw::log::Logger::get("method");
}

tbox::fw::log::Logger SomeipLogAdapter::event() {
    return tbox::fw::log::Logger::get("event");
}

tbox::fw::log::Logger SomeipLogAdapter::ipc_bridge() {
    return tbox::fw::log::Logger::get("ipc_bridge");
}

tbox::fw::log::Logger SomeipLogAdapter::serializer() {
    return tbox::fw::log::Logger::get("serializer");
}

tbox::fw::log::Logger SomeipLogAdapter::vehicle_message() {
    return tbox::fw::log::Logger::get("vehicle_message");
}

} // namespace someip
} // namespace tbox
