#pragma once

#include "cw/render/texture_bmp.hpp"

namespace cw::render {

/// 解析极简 SVG（根 `<svg>` 与 `<line>`），将描边光栅化为 **白线 + 透明底** 的 RGBA 纹理，
/// 供固定管线 `GL_MODULATE` 与 `glColor` 着色。无第三方库。
[[nodiscard]] bool load_svg_line_icon_texture(const char* path_utf8, Texture2DRgb& out,
                                              int raster_px = 128) noexcept;

}  // namespace cw::render
