#pragma once

#include <cstdint>

namespace cw {

/// Stable error taxonomy for public APIs (extend as needed).
enum class Error : std::int32_t {
  Ok = 0,
  InvalidArgument = 1,
  IOError = 2,
  Internal = 3,
  /// 例如尚未 save_snapshot 即调用 restore_snapshot。
  NoSnapshot = 4,
  /// 想定文本或结构校验失败。
  ParseError = 5,
};

constexpr bool ok(Error e) noexcept { return e == Error::Ok; }

}  // namespace cw
