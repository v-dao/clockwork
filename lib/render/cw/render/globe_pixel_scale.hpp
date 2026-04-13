#pragma once

#include <algorithm>

namespace cw::render {

/// 经纬网 / 球面标签的位图缩放系数上下限（多模块共用，避免魔数分叉）。
inline constexpr float kGlobeLabelPixelScaleMin = 0.38F;
inline constexpr float kGlobeLabelPixelScaleMax = 2.2F;

[[nodiscard]] inline float clamp_globe_label_pixel_scale(float pixel_scale) noexcept {
  return std::clamp(pixel_scale, kGlobeLabelPixelScaleMin, kGlobeLabelPixelScaleMax);
}

}  // namespace cw::render
