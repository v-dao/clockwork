#pragma once

#include <string_view>

namespace cw {

enum class LogLevel { Trace, Debug, Info, Warn, Error };

/// Writes one line to stderr with local wall-clock timestamp and level prefix.
/// Thread-safe enough for bootstrap / phase-0.
void log(LogLevel level, std::string_view message) noexcept;

}  // namespace cw
