#pragma once

#include <cstdint>

namespace cw::render {

/// 已上传到 OpenGL 的2D RGB 纹理（阶段 4底图；来自24 位未压缩 BMP）。
struct Texture2DRgb {
  unsigned gl_name = 0;
  int width = 0;
  int height = 0;
  [[nodiscard]] bool valid() const noexcept { return gl_name != 0; }
};

/// 仅支持 Windows BMP、24 bpp、BI_RGB、自下而上扫描。
[[nodiscard]] bool load_texture_bmp_rgb24(const char* path_utf8, Texture2DRgb& out) noexcept;

/// 同上，但将接近白色的像素 alpha 置 0（用于白底精灵贴图）。
[[nodiscard]] bool load_texture_bmp_rgb24_white_key(const char* path_utf8, Texture2DRgb& out) noexcept;

/// 将 RGBA8 像素上传到 2D 纹理（须已有当前 OpenGL 上下文）。
[[nodiscard]] bool upload_texture_rgba(const std::uint8_t* rgba, int w, int h, Texture2DRgb& out) noexcept;

void destroy_texture_2d(Texture2DRgb& tex) noexcept;

}  // namespace cw::render
