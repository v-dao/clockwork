#pragma once

#include "cw/render/lonlat_grid.hpp"
#include "cw/situation_view/icon_texture_cache.hpp"
#include "cw/situation_view/situation_hud.hpp"
#include "cw/situation_view/situation_view_shell.hpp"

#include <GL/gl.h>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace cw::engine {
class Engine;
}
namespace cw::render {
class WorldVectorMerc;
class WorldVectorLines;
}

namespace cw::situation_view {

/// 地图/地球绘制开关（多程序复用时按需配置）。
struct SituationRenderOptions {
  /// true：显示矢量陆块填充、等距柱 BMP、墨卡托陆栅格等；false 时仅洋面 + 岸线/国界线。
  bool show_land_basemap = false;
};

void draw_frame_tactical(const cw::engine::Engine& eng, SituationViewShell& shell, int vp_w, int vp_h,
                         int cursor_mx, int cursor_my, const cw::render::WorldVectorMerc* world_vec,
                         unsigned world_tex_gl, const cw::render::WorldVectorLines* coastlines,
                         const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                         bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base,
                         bool clear_color_buffer, const SituationRenderOptions& opts);

/// `vp_w`/`vp_h`：当前 OpenGL 地球视口；`tactical_frustum_vp_w`：与战术图 `compute_interactive_frustum` 一致的宽度（分屏时为左半宽，与纯三维整窗不同）。
void draw_frame_globe(const cw::engine::Engine& eng, SituationViewShell& shell, int vp_w, int vp_h,
                      int tactical_frustum_vp_w, int cursor_mx, int cursor_my,
                      const cw::render::WorldVectorMerc* world_vec, unsigned world_tex_gl,
                      const cw::render::WorldVectorLines* coastlines,
                      const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                      bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base,
                      bool clear_buffers, const SituationRenderOptions& opts);

void draw_frame_split(const cw::engine::Engine& eng, SituationViewShell& shell, int vp_w, int vp_h,
                      int cursor_mx, int cursor_my, const cw::render::WorldVectorMerc* world_vec,
                      unsigned world_tex_gl, const cw::render::WorldVectorLines* coastlines,
                      const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                      bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base,
                      const SituationRenderOptions& opts);

void draw_frame(const cw::engine::Engine& eng, SituationViewShell& shell, int vp_w, int vp_h, int cursor_mx,
                int cursor_my, const cw::render::WorldVectorMerc* world_vec, unsigned world_tex_gl,
                const cw::render::WorldVectorLines* coastlines,
                const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base,
                const SituationRenderOptions& opts);

void draw_split_divider(int vp_w, int vp_h, int split_x);

#ifdef _WIN32
[[nodiscard]] GLuint create_hud_bitmap_font_lists(HDC hdc);
void draw_hud_gl(int vp_w, int vp_h, GLuint font_base, const SituationHud& hud,
                 const std::vector<cw::render::GlobeLonLatLabel>* grid_labels);
/// 仿真时间、倍速、引擎状态；可选右上角实体简表（`show_entity_list` 且态势中有实体时）。
void draw_simulation_overlay_gl(int vp_w, int vp_h, GLuint font_base, const cw::engine::Engine& eng,
                                bool show_entity_list);
#endif

}  // namespace cw::situation_view
