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
  /// 引擎当前状态不允许该操作（如未 initialize 即 start、非 Running 时 pause）。
  WrongState = 6,
  /// 联邦模式下不允许保存/恢复快照。
  NotAllowedWhenFederated = 7,
  /// 想定 version 不是引擎支持的值（当前仅 1 与 2）。
  UnsupportedScenarioVersion = 8,
};

constexpr bool ok(Error e) noexcept { return e == Error::Ok; }

/// 稳定英文标识，便于日志检索与脚本判断（与枚举值一一对应）。
[[nodiscard]] const char* error_code_str(Error e) noexcept;

/// 面向排障的简短中文说明（与 `error_code_str` 描述同一错误）。
[[nodiscard]] const char* error_message(Error e) noexcept;

}  // namespace cw
