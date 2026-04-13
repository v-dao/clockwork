#pragma once

#include "cw/error.hpp"

#include <string_view>

namespace cw {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

/// Writes one line to stderr with local wall-clock timestamp and level prefix.
/// Thread-safe enough for bootstrap / phase-0.
void log(LogLevel level, std::string_view message) noexcept;

/// 将 API 失败记一行日志：`operation_context` +错误码英文名 + 中文说明。
void log_error(std::string_view operation_context, Error e) noexcept;

}  // namespace cw
