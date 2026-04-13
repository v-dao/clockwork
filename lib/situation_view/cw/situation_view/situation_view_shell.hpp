#pragma once

#include "cw/engine/situation.hpp"
#include "cw/engine/types.hpp"
#include "cw/render/globe_view_3d.hpp"
#include "cw/render/tactical_map_2d.hpp"

#include <optional>

namespace cw::engine {
class Engine;
}

namespace cw::render {
class GlWindow;
}

namespace cw::situation_view {

class Win32SituationChrome;

/// 与 `situation_view` 主循环配套的视图模式。
enum class ViewMode { Tactical2D, Globe3d, Split2dGlobe };

/// 客户端坐标系（左上原点）下的子矩形，作为二维/三维「地图窗口」：鼠标先落在此层，再换算为子视口局部像素。
struct MapWindow {
  int x = 0;
  int y = 0;
  int w = 1;
  int h = 1;
  [[nodiscard]] bool contains(int mx, int my) const noexcept {
    return mx >= x && mx < x + w && my >= y && my < y + h;
  }
  [[nodiscard]] int to_local_x(int mx) const noexcept { return mx - x; }
  [[nodiscard]] int to_local_y(int my) const noexcept { return my - y; }
};

/// 封装战术图/地球相机状态、鼠标拖动与滚轮、分屏中心与比例尺同步；原生菜单由 `SituationViewChrome` 负责。
class SituationViewShell {
 public:
  [[nodiscard]] cw::render::TacticalMercatorMap& tactical_map() noexcept { return tactical_; }
  [[nodiscard]] const cw::render::TacticalMercatorMap& tactical_map() const noexcept { return tactical_; }
  [[nodiscard]] cw::render::GlobeEarthView& globe_view() noexcept { return globe_; }
  [[nodiscard]] const cw::render::GlobeEarthView& globe_view() const noexcept { return globe_; }

  [[nodiscard]] ViewMode view_mode() const noexcept { return view_mode_; }
  void set_view_mode(ViewMode m) noexcept { view_mode_ = m; }

  /// 当前模式下战术墨卡托子区域（分屏为左半窗，否则整窗）。
  [[nodiscard]] MapWindow tactical_map_window(int client_w, int client_h) const noexcept;
  /// 当前模式下三维地球子区域（分屏为右半窗，否则整窗）。
  [[nodiscard]] MapWindow globe_map_window(int client_w, int client_h) const noexcept;

  void reset_view_camera() noexcept;
  void reset_globe_auxiliary_state() noexcept { globe_.reset_content_orientation(); }

  /// 左键拖动平移 / 弧球；分屏时设置 `split_*_driven`（在仿真步进之前调用）。
  void process_mouse_drag(cw::render::GlWindow& win, const cw::engine::SituationPresentation& world,
                          bool& split_left_driven, bool& split_right_driven);

  /// 左键点击仿真实体拾取（与拖动区分：位移小于约 8px 视为点击）。须在 `poll_events` 之后调用。
  void process_entity_pick_mouse(const cw::engine::SituationPresentation& world, int client_w, int client_h,
                                 int mouse_x, int mouse_y, bool left_down, bool left_was_down);
  [[nodiscard]] std::optional<cw::engine::EntityId> picked_entity_id() const noexcept {
    return picked_entity_id_;
  }
  /// 滚轮缩放（在仿真步进之后调用，与原先主循环顺序一致）。
  void process_wheel(cw::render::GlWindow& win, bool& split_left_driven, bool& split_right_driven);

  /// 分屏模式下、`draw_frame` 之前：弧球刷新、二三维中心与东西向比例尺同步（由 `process_pointer_and_wheel` 置位驱动侧）。
  void pre_draw_split_sync(const cw::engine::SituationPresentation& world, int client_w, int client_h,
                           bool split_left_driven, bool split_right_driven);

  /// 分屏时与左侧战术图一致的经纬网格步长（度）；非分屏或未更新时为 0。
  [[nodiscard]] double split_matched_lonlat_grid_step_deg() const noexcept {
    return split_matched_lonlat_grid_step_deg_;
  }

  /// 供二三维切换时同步视口（场景外包络依赖引擎）；建议在创建 `Engine` 后始终设置。
  void set_viewport_sync_engine(cw::engine::Engine* engine) noexcept { viewport_sync_engine_ = engine; }

 private:
  void sync_globe_from_tactical_viewport(const cw::engine::SituationPresentation& world, int client_w,
                                         int client_h, int tactical_frustum_vp_w) noexcept;
  void sync_tactical_from_globe_viewport(const cw::engine::SituationPresentation& world, int client_w,
                                         int client_h, int globe_vp_w_for_ew_readout) noexcept;

  friend class Win32SituationChrome;

  cw::render::TacticalMercatorMap tactical_{};
  cw::render::GlobeEarthView globe_{};
  ViewMode view_mode_ = ViewMode::Tactical2D;
  cw::engine::Engine* viewport_sync_engine_ = nullptr;
  bool split_initial_sync_pending_ = false;
  /// 分屏时是否把战术图东西向宽度与地球透视水平宽度互相同步（默认开）。
  bool split_scale_sync_enabled_ = true;
  double split_matched_lonlat_grid_step_deg_ = 0.;
  bool drag_prev_valid_ = false;
  int drag_prev_mx_ = 0;
  int drag_prev_my_ = 0;
  int pick_down_mx_ = 0;
  int pick_down_my_ = 0;
  bool pick_drag_cancel_ = false;
  std::optional<cw::engine::EntityId> picked_entity_id_{};
};

}  // namespace cw::situation_view
