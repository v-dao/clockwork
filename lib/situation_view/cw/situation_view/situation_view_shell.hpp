#pragma once

#include "cw/render/globe_view_3d.hpp"
#include "cw/render/tactical_map_2d.hpp"

namespace cw::engine {
class Engine;
}

namespace cw::render {
class GlWindow;
}

namespace cw::scenario {
struct Scenario;
}

namespace cw::situation_view {

/// 与 `situation_view` 主循环配套的视图模式。
enum class ViewMode { Tactical2D, Globe3d, Split2dGlobe };

/// 封装战术图/地球相机状态、Win32 视图菜单、鼠标拖动与滚轮、分屏中心与比例尺同步。
class SituationViewShell {
 public:
  [[nodiscard]] cw::render::TacticalMercatorMap& tactical_map() noexcept { return tactical_; }
  [[nodiscard]] const cw::render::TacticalMercatorMap& tactical_map() const noexcept { return tactical_; }
  [[nodiscard]] cw::render::GlobeEarthView& globe_view() noexcept { return globe_; }
  [[nodiscard]] const cw::render::GlobeEarthView& globe_view() const noexcept { return globe_; }

  [[nodiscard]] ViewMode view_mode() const noexcept { return view_mode_; }
  void set_view_mode(ViewMode m) noexcept { view_mode_ = m; }

  void reset_view_camera() noexcept;
  void reset_globe_auxiliary_state() noexcept { globe_.reset_content_orientation(); }

#ifdef _WIN32
  /// 在已打开的 `GlWindow` 上创建 View 菜单并注册 `WM_COMMAND` 回调。
  void install_win32_view_menu(cw::render::GlWindow& win);
  /// 在已有菜单栏上追加 **Simulation**（暂停 / 继续 / 结束 / 倍速）。须先调用 `install_win32_view_menu`。
  void install_win32_simulation_menu(cw::render::GlWindow& win);
  /// 供菜单回调使用；若未设置则仿真菜单项无效。
  void set_simulation_menu_engine(cw::engine::Engine* engine) noexcept { engine_sim_menu_ = engine; }
  /// 供 Reset simulation 菜单再次 `apply_scenario`；仅地图模式可不设。
  void set_scenario_for_reset(const cw::scenario::Scenario* scenario) noexcept {
    scenario_for_reset_ = scenario;
  }
  /// 将倍速单选与 `engine.time_scale()` 对齐（如键盘改倍速后刷新菜单）。
  void sync_simulation_menu_from_engine();
#endif

  /// 左键拖动平移 / 弧球；分屏时设置 `split_*_driven`（在仿真步进之前调用）。
  void process_mouse_drag(cw::render::GlWindow& win, cw::engine::Engine& engine, bool& split_left_driven,
                          bool& split_right_driven);
  /// 滚轮缩放（在仿真步进之后调用，与原先主循环顺序一致）。
  void process_wheel(cw::render::GlWindow& win, bool& split_left_driven, bool& split_right_driven);

  /// 分屏模式下、`draw_frame` 之前：弧球刷新、二三维中心与东西向比例尺同步（由 `process_pointer_and_wheel` 置位驱动侧）。
  void pre_draw_split_sync(cw::engine::Engine& engine, int client_w, int client_h, bool split_left_driven,
                           bool split_right_driven);

  /// 分屏时与左侧战术图一致的经纬网格步长（度）；非分屏或未更新时为 0。
  [[nodiscard]] double split_matched_lonlat_grid_step_deg() const noexcept {
    return split_matched_lonlat_grid_step_deg_;
  }

 private:
#ifdef _WIN32
  void on_win32_menu_command(unsigned cmd);
  static void win32_menu_thunk(unsigned cmd, void* user);
#endif

  cw::render::TacticalMercatorMap tactical_{};
  cw::render::GlobeEarthView globe_{};
  ViewMode view_mode_ = ViewMode::Tactical2D;
  bool split_initial_sync_pending_ = false;
  /// 分屏时是否把战术图东西向宽度与地球透视水平宽度互相同步（默认开）。
  bool split_scale_sync_enabled_ = true;
  double split_matched_lonlat_grid_step_deg_ = 0.;
  bool drag_prev_valid_ = false;
  int drag_prev_mx_ = 0;
  int drag_prev_my_ = 0;
#ifdef _WIN32
  void* hwnd_main_ = nullptr;
  void* hmenu_view_ = nullptr;
  void* hmenu_sim_ = nullptr;
  cw::engine::Engine* engine_sim_menu_ = nullptr;
  const cw::scenario::Scenario* scenario_for_reset_ = nullptr;
#endif
};

}  // namespace cw::situation_view
