#pragma once

/**
 * TBOX-SOMEIP 上下文传播辅助
 *
 * 使用 framework-log 提供的 ContextScope 和 LogContext
 * 本头文件仅提供便捷的工厂函数和辅助函数
 */

#include "log.h"
#include "log_types.h"
#include <string>

namespace tbox {
namespace someip {

/**
 * 创建 ContextScope 的便捷函数
 *
 * 用法：
 *   auto scope = make_context_scope(trace_id, request_id, session_id);
 *   // scope 离开作用域时自动清理上下文
 */
[[nodiscard]] inline tbox::fw::log::ContextScope make_context_scope(
    const std::string& trace_id,
    const std::string& request_id,
    const std::string& session_id = ""
) {
    tbox::fw::log::LogContext ctx;
    ctx.trace_id = trace_id;
    ctx.request_id = request_id;
    ctx.session_id = session_id;
    return tbox::fw::log::ContextScope(ctx);
}

/**
 * 生成请求 ID（如果 SOME/IP 请求或 IPC 请求中缺少）
 * 格式: someip-<timestamp>-<random>
 */
std::string generate_request_id();

} // namespace someip
} // namespace tbox
