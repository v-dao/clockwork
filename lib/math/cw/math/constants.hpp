#pragma once

#include <numbers>

namespace cw::math {

/// 双精度 π（C++20 `std::numbers`）。
inline constexpr double kPi = std::numbers::pi;

/// 单精度 π。
inline constexpr float kPiF = std::numbers::pi_v<float>;

}  // namespace cw::math
