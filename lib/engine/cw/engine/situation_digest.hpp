#pragma once

#include <cstdint>

#include "cw/engine/situation.hpp"

namespace cw::engine {

/// 对 `SituationSnapshot` 做规范排序后的 FNV-1a 64 位摘要（实体按 `id`、探测按观察者/目标及量测位模式排序）。
/// 用于固定步数下的回归对拍；跨编译器/平台的浮点差异仍可能存在，金样以当前工具链为准。
[[nodiscard]] std::uint64_t situation_digest(const SituationSnapshot& snap) noexcept;

}  // namespace cw::engine
