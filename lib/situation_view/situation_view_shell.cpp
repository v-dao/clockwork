#include "cw/situation_view/situation_view_shell.hpp"

#include "cw/engine/engine.hpp"
#include "cw/render/gl_window.hpp"
#include "cw/render/lonlat_grid.hpp"
#include "cw/render/mercator_geo.hpp"

#include <cmath>
#include <algorithm>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <GL/gl.h>

namespace cw::situation_view {

void SituationViewShell::reset_view_camera() noexcept {
  tactical_.reset_camera();
  drag_prev_valid_ = false;
  globe_.reset_content_orientation();
}

#ifdef _WIN32
namespace {

constexpr unsigned kMenuView2d = 0xE100;
constexpr unsigned kMenuView3d = 0xE101;
constexpr unsigned kMenuViewSplit2d3d = 0xE103;
constexpr unsigned kMenuGlobeLighting = 0xE102;
constexpr unsigned kMenuSplitScaleSync = 0xE104;
constexpr unsigned kMenuTacticalAutoBounds = 0xE105;

constexpr unsigned kMenuSimPause = 0xE200;
constexpr unsigned kMenuSimResume = 0xE201;
constexpr unsigned kMenuSimEnd = 0xE202;
constexpr unsigned kMenuSimReset = 0xE203;
constexpr unsigned kMenuSimSpeed025 = 0xE210;
constexpr unsigned kMenuSimSpeed05 = 0xE211;
constexpr unsigned kMenuSimSpeed1 = 0xE212;
constexpr unsigned kMenuSimSpeed2 = 0xE213;
constexpr unsigned kMenuSimSpeed4 = 0xE214;

}  // namespace

void SituationViewShell::win32_menu_thunk(unsigned cmd, void* user) {
  if (user != nullptr) {
    static_cast<SituationViewShell*>(user)->on_win32_menu_command(cmd);
  }
}

void SituationViewShell::on_win32_menu_command(unsigned cmd) {
  HMENU hview = static_cast<HMENU>(hmenu_view_);
  if (cmd == kMenuView2d) {
    view_mode_ = ViewMode::Tactical2D;
    if (hview != nullptr) {
      CheckMenuRadioItem(hview, kMenuView2d, kMenuViewSplit2d3d, kMenuView2d, MF_BYCOMMAND);
    }
  } else if (cmd == kMenuView3d) {
    view_mode_ = ViewMode::Globe3d;
    if (hview != nullptr) {
      CheckMenuRadioItem(hview, kMenuView2d, kMenuViewSplit2d3d, kMenuView3d, MF_BYCOMMAND);
    }
  } else if (cmd == kMenuViewSplit2d3d) {
    view_mode_ = ViewMode::Split2dGlobe;
    split_initial_sync_pending_ = true;
    if (hview != nullptr) {
      CheckMenuRadioItem(hview, kMenuView2d, kMenuViewSplit2d3d, kMenuViewSplit2d3d, MF_BYCOMMAND);
    }
  } else if (cmd == kMenuGlobeLighting) {
    globe_.toggle_lighting();
    if (hview != nullptr) {
      CheckMenuItem(hview, static_cast<UINT>(kMenuGlobeLighting),
                    MF_BYCOMMAND | (globe_.lighting_enabled() ? MF_CHECKED : MF_UNCHECKED));
    }
  } else if (cmd == kMenuSplitScaleSync) {
    split_scale_sync_enabled_ = !split_scale_sync_enabled_;
    if (hview != nullptr) {
      CheckMenuItem(hview, static_cast<UINT>(kMenuSplitScaleSync),
                    MF_BYCOMMAND | (split_scale_sync_enabled_ ? MF_CHECKED : MF_UNCHECKED));
    }
  } else if (cmd == kMenuTacticalAutoBounds) {
    const bool v = !tactical_.auto_bounds_include_entities();
    tactical_.set_auto_bounds_include_entities(v);
    if (hview != nullptr) {
      CheckMenuItem(hview, static_cast<UINT>(kMenuTacticalAutoBounds),
                    MF_BYCOMMAND | (v ? MF_CHECKED : MF_UNCHECKED));
    }
  } else if (cmd == kMenuSimPause) {
    if (engine_sim_menu_ != nullptr) {
      static_cast<void>(engine_sim_menu_->pause());
    }
  } else if (cmd == kMenuSimResume) {
    if (engine_sim_menu_ != nullptr) {
      static_cast<void>(engine_sim_menu_->start());
    }
  } else if (cmd == kMenuSimEnd) {
    if (engine_sim_menu_ != nullptr) {
      static_cast<void>(engine_sim_menu_->end());
    }
  } else if (cmd == kMenuSimReset) {
    if (engine_sim_menu_ != nullptr && scenario_for_reset_ != nullptr) {
      static_cast<void>(engine_sim_menu_->reset_with_scenario(*scenario_for_reset_));
    }
  } else if (cmd >= kMenuSimSpeed025 && cmd <= kMenuSimSpeed4) {
    if (engine_sim_menu_ != nullptr) {
      static constexpr double kScales[] = {0.25, 0.5, 1.0, 2.0, 4.0};
      const unsigned idx = cmd - kMenuSimSpeed025;
      if (idx < sizeof(kScales) / sizeof(kScales[0])) {
        static_cast<void>(engine_sim_menu_->set_time_scale(kScales[idx]));
      }
      HMENU hsim = static_cast<HMENU>(hmenu_sim_);
      if (hsim != nullptr) {
        CheckMenuRadioItem(hsim, kMenuSimSpeed025, kMenuSimSpeed4, static_cast<UINT>(cmd), MF_BYCOMMAND);
      }
    }
  }
}

void SituationViewShell::install_win32_view_menu(cw::render::GlWindow& win) {
  hwnd_main_ = win.win32_hwnd();
  HMENU h_bar = CreateMenu();
  HMENU h_view = CreateMenu();
  hmenu_view_ = h_view;
  AppendMenuW(h_view, MF_STRING | MF_CHECKED, static_cast<UINT_PTR>(kMenuView2d), L"2D tactical map");
  AppendMenuW(h_view, MF_STRING, static_cast<UINT_PTR>(kMenuView3d), L"3D globe");
  AppendMenuW(h_view, MF_STRING, static_cast<UINT_PTR>(kMenuViewSplit2d3d), L"2D + 3D split view");
  AppendMenuW(h_view, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(h_view, MF_STRING, static_cast<UINT_PTR>(kMenuGlobeLighting), L"三维地图光照");
  AppendMenuW(h_view, MF_STRING | MF_CHECKED, static_cast<UINT_PTR>(kMenuSplitScaleSync),
              L"分屏左右比例尺同步");
  AppendMenuW(h_view, MF_STRING, static_cast<UINT_PTR>(kMenuTacticalAutoBounds),
              L"战术自动框选实体");
  AppendMenuW(h_bar, MF_POPUP, reinterpret_cast<UINT_PTR>(h_view), L"View");
  SetMenu(static_cast<HWND>(hwnd_main_), h_bar);
  DrawMenuBar(static_cast<HWND>(hwnd_main_));
  win.set_menu_command_callback(&SituationViewShell::win32_menu_thunk, this);
}

void SituationViewShell::install_win32_simulation_menu(cw::render::GlWindow& win) {
  hwnd_main_ = win.win32_hwnd();
  HMENU h_bar = GetMenu(static_cast<HWND>(hwnd_main_));
  if (h_bar == nullptr) {
    return;
  }
  HMENU h_sim = CreateMenu();
  hmenu_sim_ = h_sim;
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimPause), L"Pause");
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimResume), L"Resume");
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimEnd), L"End simulation");
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimReset), L"Reset simulation");
  AppendMenuW(h_sim, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimSpeed025), L"Time scale 0.25x");
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimSpeed05), L"Time scale 0.5x");
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimSpeed1), L"Time scale 1x");
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimSpeed2), L"Time scale 2x");
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimSpeed4), L"Time scale 4x");
  CheckMenuRadioItem(h_sim, kMenuSimSpeed025, kMenuSimSpeed4, kMenuSimSpeed1, MF_BYCOMMAND);
  AppendMenuW(h_bar, MF_POPUP, reinterpret_cast<UINT_PTR>(h_sim), L"Simulation");
  DrawMenuBar(static_cast<HWND>(hwnd_main_));
}

void SituationViewShell::sync_simulation_menu_from_engine() {
  if (engine_sim_menu_ == nullptr || hmenu_sim_ == nullptr) {
    return;
  }
  HMENU h_sim = static_cast<HMENU>(hmenu_sim_);
  const double ts = engine_sim_menu_->time_scale();
  static constexpr double kScales[] = {0.25, 0.5, 1.0, 2.0, 4.0};
  unsigned cmd = kMenuSimSpeed1;
  for (unsigned i = 0; i < sizeof(kScales) / sizeof(kScales[0]); ++i) {
    if (std::fabs(ts - kScales[i]) < 1e-5) {
      cmd = kMenuSimSpeed025 + i;
      break;
    }
  }
  CheckMenuRadioItem(h_sim, kMenuSimSpeed025, kMenuSimSpeed4, cmd, MF_BYCOMMAND);
}
#else
void SituationViewShell::install_win32_view_menu(cw::render::GlWindow&) {}
void SituationViewShell::install_win32_simulation_menu(cw::render::GlWindow&) {}
void SituationViewShell::sync_simulation_menu_from_engine() {}
#endif

void SituationViewShell::process_mouse_drag(cw::render::GlWindow& win, cw::engine::Engine& engine,
                                            bool& split_left_driven, bool& split_right_driven) {
  const int cw = win.client_width();
  const int ch = win.client_height();

  if (win.left_button_down()) {
    if (drag_prev_valid_) {
      const int split_x = std::max(1, cw / 2);
      const int mx = win.mouse_client_x();
      /// 仅分屏时按 x 划分左右；纯 2D / 纯 3D 不得用「非分屏 => 左半屏」否则 Globe3d 会恒走战术平移、弧球永远不触发。
      const bool split_left =
          (view_mode_ == ViewMode::Split2dGlobe) && (drag_prev_mx_ < split_x);
      const bool split_right =
          (view_mode_ == ViewMode::Split2dGlobe) && (drag_prev_mx_ >= split_x);
      if (view_mode_ == ViewMode::Tactical2D || split_left) {
        const int dx = mx - drag_prev_mx_;
        const int dy = win.mouse_client_y() - drag_prev_my_;
        const int pan_w = (view_mode_ == ViewMode::Split2dGlobe) ? split_x : cw;
        tactical_.apply_mouse_pan_drag(engine, pan_w, ch, dx, dy);
        if (view_mode_ == ViewMode::Split2dGlobe && (dx != 0 || dy != 0)) {
          split_left_driven = true;
        }
      } else if (view_mode_ == ViewMode::Globe3d || split_right) {
        const int pmx = drag_prev_mx_;
        const int pmy = drag_prev_my_;
        const int cmx = mx;
        const int cmy = win.mouse_client_y();
        if (pmx != cmx || pmy != cmy) {
          if (view_mode_ == ViewMode::Split2dGlobe) {
            globe_.queue_arcball_drag(pmx - split_x, pmy, cmx - split_x, cmy);
          } else {
            globe_.queue_arcball_drag(pmx, pmy, cmx, cmy);
          }
          if (view_mode_ == ViewMode::Split2dGlobe) {
            split_right_driven = true;
          }
        }
      }
    }
    drag_prev_mx_ = win.mouse_client_x();
    drag_prev_my_ = win.mouse_client_y();
    drag_prev_valid_ = true;
  } else {
    drag_prev_valid_ = false;
    globe_.clear_arcball_pending();
  }
}

void SituationViewShell::process_wheel(cw::render::GlWindow& win, bool& split_left_driven,
                                       bool& split_right_driven) {
  const int cw = win.client_width();
  const int wd = win.consume_wheel_delta();
  if (wd != 0) {
    const int split_x = std::max(1, cw / 2);
    const int mx = win.mouse_client_x();
    if (view_mode_ == ViewMode::Split2dGlobe) {
      if (mx < split_x) {
        split_left_driven = true;
      } else {
        split_right_driven = true;
      }
    }
    const bool wheel_on_globe =
        (view_mode_ == ViewMode::Globe3d) || (view_mode_ == ViewMode::Split2dGlobe && mx >= split_x);
    if (wheel_on_globe) {
      constexpr float kGlobeDistMin = 1.00002F;
      constexpr float kGlobeDistMax = 28.F;
      constexpr float kWheelHRef = 2.2F;
      constexpr float kStepFar = 1.2F;
      constexpr float kStepNear = 1.1F;
      const float h = std::max(globe_.camera().distance - 1.0F, 1.0e-8F);
      const float t = std::clamp(h / kWheelHRef, 0.0F, 1.0F);
      const float kStep = kStepNear + (kStepFar - kStepNear) * t;
      const float h_new = (wd > 0) ? (h / kStep) : (h * kStep);
      const float dist = 1.0F + h_new;
      globe_.camera().distance = std::clamp(dist, kGlobeDistMin, kGlobeDistMax);
    } else {
      tactical_.apply_wheel_zoom(wd);
    }
  }
}

void SituationViewShell::pre_draw_split_sync(cw::engine::Engine& engine, int client_w, int client_h,
                                             bool split_left_driven, bool split_right_driven) {
  if (view_mode_ != ViewMode::Split2dGlobe) {
    split_matched_lonlat_grid_step_deg_ = 0.;
    return;
  }
  const int split_x = std::max(1, client_w / 2);
  const int right_w = std::max(1, client_w - split_x);
  if (globe_.arcball_pending()) {
    glViewport(split_x, 0, right_w, client_h);
    globe_.setup_projection_and_modelview(right_w, client_h);
    glViewport(0, 0, client_w, client_h);
  }
  cw::render::MercatorBounds b{};
  tactical_.expand_bounds_from_engine(engine, b);
  cw::render::MercatorOrthoFrustum tact{};
  tactical_.compute_interactive_frustum(b, split_x, client_h, tact);
  {
    const float cx_ref = (tact.l + tact.r) * 0.5F;
    const double span = cw::render::tactical_frustum_lonlat_span_deg(tact, cx_ref);
    const float equiv_d = cw::render::tactical_equiv_camera_distance_from_span_deg(span);
    split_matched_lonlat_grid_step_deg_ = cw::render::pick_lonlat_step_deg(span, equiv_d);
  }
  const double tcx = static_cast<double>((tact.l + tact.r) * 0.5F);
  const double tcy = static_cast<double>((tact.b + tact.t) * 0.5F);
  double t_lon = 0.;
  double t_lat = 0.;
  cw::render::mercator_meters_to_lonlat(tcx, tcy, t_lon, t_lat);
  double g_lon = 0.;
  double g_lat = 0.;
  globe_.viewport_center_lonlat_from_pose(g_lon, g_lat);

  constexpr double kPi = 3.14159265358979323846;
  auto tactical_center_ew_m = [&tact, kPi](double lat_deg) -> double {
    const double lat_r = lat_deg * (kPi / 180.0);
    return static_cast<double>(tact.r - tact.l) * std::max(1e-4, std::cos(lat_r));
  };
  auto sync_scale_left_to_right = [this, t_lat, right_w, client_h, tactical_center_ew_m]() {
    const double ew = tactical_center_ew_m(t_lat);
    if (ew > 1.0) {
      globe_.set_camera_distance_for_visible_ew_meters(right_w, client_h, ew);
    }
  };
  auto sync_scale_right_to_left = [this, &engine, split_x, client_h, g_lat, right_w]() {
    const double ew = globe_.visible_ground_ew_meters(right_w, client_h);
    if (ew > 1.0) {
      tactical_.set_visible_ground_ew_meters_at_lat(engine, split_x, client_h, ew, g_lat);
    }
  };

  /// 缩放：战术图东西向地面宽度 `(r-l)*cos(φ)` 与球面透视水平视场宽度对齐。
  /// 仅在「纯右侧」驱动时由地球反推战术 zoom；其余情况（含无操作帧）均以战术为准更新地球距离，
  /// 避免想定范围变化或漏同步帧后两侧倍率长期不一致。
  const bool only_right = split_right_driven && !split_left_driven;

  if (split_initial_sync_pending_) {
    globe_.orient_content_to_place_lonlat_at_screen_center(t_lon, t_lat);
    split_initial_sync_pending_ = false;
    if (split_scale_sync_enabled_) {
      sync_scale_left_to_right();
    }
  } else if (only_right) {
    tactical_.set_frustum_center_lonlat(engine, split_x, client_h, g_lon, g_lat);
    if (split_scale_sync_enabled_) {
      sync_scale_right_to_left();
    }
  } else {
    if (split_left_driven || split_right_driven) {
      globe_.orient_content_to_place_lonlat_at_screen_center(t_lon, t_lat);
    }
    if (split_scale_sync_enabled_) {
      sync_scale_left_to_right();
    }
  }
}

}  // namespace cw::situation_view