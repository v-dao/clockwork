#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif
#include <GL/gl.h>

#include <utility>
#include <vector>

namespace cw::render {

struct WorldVectorMercPolygon {
  std::vector<std::vector<std::pair<float, float>>> rings;
};

/// Web 墨卡托米制陆地多边形（由 scripts/build_world_vector_merc.py 生成 .merc2）。
/// 须在已创建当前 OpenGL 上下文后调用 load_from_file（使用 GLU 细分写入显示列表）。
struct WorldVectorMerc {
  GLuint land_display_list = 0;
  /// 与显示列表同源，用于三维球面填充。
  std::vector<WorldVectorMercPolygon> polygons;

  [[nodiscard]] bool valid() const noexcept { return land_display_list != 0; }

  [[nodiscard]] bool load_from_file(const char* path_utf8) noexcept;

  void draw_land_fill() const noexcept;

  /// 在单位球面（半径 `radius`）上绘制陆块填充，与 2D `draw_land_fill` 几何一致。
  void draw_land_fill_sphere(double radius) const noexcept;

  void destroy() noexcept;
};

}  // namespace cw::render
