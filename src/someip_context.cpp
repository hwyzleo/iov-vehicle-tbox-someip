#include "tbox_someip/someip_context.h"
#include <chrono>
#include <random>
#include <sstream>
#include <iomanip>

namespace tbox {
namespace someip {

std::string generate_request_id() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis(0, 0xFFFFFFFF);

    std::ostringstream oss;
    oss << "someip-" << timestamp << "-" << std::hex << dis(gen);
    return oss.str();
}

} // namespace someip
} // namespace tbox
