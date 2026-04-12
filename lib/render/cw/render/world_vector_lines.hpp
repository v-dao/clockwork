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

/// Web 墨卡托米制折线（国界等），由 scripts/build_boundary_lines_mercl.py 生成 .mercl（CWl1）。
struct WorldVectorLines {
  GLuint line_display_list = 0;
  /// 与显示列表同源，用于三维球面绘制。
  std::vector<std::vector<std::pair<float, float>>> strips;

  [[nodiscard]] bool valid() const noexcept { return line_display_list != 0; }

  [[nodiscard]] bool load_from_file(const char* path_utf8) noexcept;

  void draw() const noexcept;

  /// 将折线投影到半径 `radius` 的球面上（与 2D 墨卡托数据一致）。
  void draw_on_unit_sphere(double radius) const noexcept;

  void destroy() noexcept;
};

}  // namespace cw::render
