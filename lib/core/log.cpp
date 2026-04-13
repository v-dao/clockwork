#include "cw/log.hpp"

#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <ctime>

namespace cw {

namespace {

const char* level_str(LogLevel level) noexcept {
  switch (level) {
    case LogLevel::Trace:
      return "TRACE";
    case LogLevel::Debug:
      return "DEBUG";
    case LogLevel::Info:
      return "INFO";
    case LogLevel::Warn:
      return "WARN";
    case LogLevel::Error:
      return "ERROR";
  }
  return "?";
}

std::mutex& log_mutex() {
  static std::mutex m;
  return m;
}

}  // namespace

void log_error(std::string_view operation_context, Error e) noexcept {
  try {
    std::string line;
    line.reserve(operation_context.size() + 64U);
    line.append(operation_context);
    line.append(" [");
    line.append(error_code_str(e));
    line.append("] ");
    line.append(error_message(e));
    log(LogLevel::Error, line);
  } catch (...) {
  }
}

void log(LogLevel level, std::string_view message) noexcept {
  try {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    const long long ms_total = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   now.time_since_epoch())
                                   .count();
    const long long subsec_ms = ((ms_total % 1000LL) + 1000LL) % 1000LL;
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif

    std::lock_guard<std::mutex> lock(log_mutex());
    // Minimal formatting; avoid iostreams static init order surprises in phase-0.
    std::fprintf(stderr, "[%04d-%02d-%02d %02d:%02d:%02d.%03lld] [%s] %.*s\n",
                 tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday, tm_buf.tm_hour,
                 tm_buf.tm_min, tm_buf.tm_sec, subsec_ms,
                 level_str(level), static_cast<int>(message.size()), message.data());
    std::fflush(stderr);
  } catch (...) {
    // Last-resort: never throw from logging.
  }
}

}  // namespace cw
