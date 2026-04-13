#pragma once

#include "cw/engine/situation.hpp"
#include "cw/engine/types.hpp"
#include "cw/render/graphics_types.hpp"
#include "cw/render/lonlat_grid.hpp"
#include "cw/situation_view/icon_texture_cache.hpp"
#include "cw/situation_view/situation_hud.hpp"
#include "cw/situation_view/situation_view_shell.hpp"

#include <GL/gl.h>
#include <optional>
#include <vector>

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

void draw_frame_tactical(const cw::engine::SituationPresentation& world, SituationViewShell& shell, int vp_w,
                         int vp_h,
                         int cursor_mx, int cursor_my, const cw::render::WorldVectorMerc* world_vec,
                         unsigned world_tex_gl, const cw::render::WorldVectorLines* coastlines,
                         const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                         bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base,
                         bool clear_color_buffer, const SituationRenderOptions& opts);

/// `vp_w`/`vp_h`：当前 OpenGL 地球视口；`tactical_frustum_vp_w`：与战术图 `compute_interactive_frustum` 一致的宽度（分屏时为左半宽，与纯三维整窗不同）。
void draw_frame_globe(const cw::engine::SituationPresentation& world, SituationViewShell& shell, int vp_w,
                      int vp_h,
                      int tactical_frustum_vp_w, int cursor_mx, int cursor_my,
                      const cw::render::WorldVectorMerc* world_vec, unsigned world_tex_gl,
                      const cw::render::WorldVectorLines* coastlines,
                      const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                      bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base,
                      bool clear_buffers, const SituationRenderOptions& opts);

void draw_frame_split(const cw::engine::SituationPresentation& world, SituationViewShell& shell, int vp_w,
                      int vp_h,
                      int cursor_mx, int cursor_my, const cw::render::WorldVectorMerc* world_vec,
                      unsigned world_tex_gl, const cw::render::WorldVectorLines* coastlines,
                      const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                      bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base,
                      const SituationRenderOptions& opts);

void draw_frame(const cw::engine::SituationPresentation& world, SituationViewShell& shell, int vp_w, int vp_h,
                int cursor_mx,
                int cursor_my, const cw::render::WorldVectorMerc* world_vec, unsigned world_tex_gl,
                const cw::render::WorldVectorLines* coastlines,
                const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base,
                const SituationRenderOptions& opts, double perf_fps, double perf_frame_ms,
                cw::render::GraphicsApi perf_present_api);

void draw_split_divider(int vp_w, int vp_h, int split_x);

/// 左键释放时调用（需在 `make_current` 后、绘制前）。战术图为纯数学拾取；三维/分屏右侧需临时设置地球投影。
[[nodiscard]] std::optional<cw::engine::EntityId> try_pick_entity_at_screen(
    const cw::engine::SituationPresentation& world, SituationViewShell& shell, int client_w, int client_h,
    int mouse_x, int mouse_y);

#ifdef _WIN32
void draw_hud_gl(int vp_w, int vp_h, GLuint font_base, const SituationHud& hud,
                 const std::vector<cw::render::GlobeLonLatLabel>* grid_labels);
/// 仿真时间、倍速、引擎状态；可选右上角实体简表（`show_entity_list` 且态势中有实体时）。
/// `detail_entity`：左键点选后左上角显示该实体属性；无则传 `std::nullopt`。
void draw_perf_overlay_gl(int vp_w, int vp_h, GLuint font_base, double fps, double frame_ms,
                          cw::render::GraphicsApi present_api);
void draw_simulation_overlay_gl(int vp_w, int vp_h, GLuint font_base,
                                const cw::engine::SituationPresentation& world, bool show_entity_list,
                                std::optional<cw::engine::EntityId> detail_entity, int extra_top_pad_px = 0);
#endif

}  // namespace cw::situation_view
