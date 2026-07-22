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
