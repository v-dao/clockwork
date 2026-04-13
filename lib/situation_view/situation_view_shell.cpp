#include "cw/situation_view/situation_view_shell.hpp"

#include "cw/situation_view/situation_map_globe_render.hpp"

#include "cw/engine/engine.hpp"
#include "cw/render/gl_window.hpp"
#include "cw/render/lonlat_grid.hpp"
#include "cw/render/mercator_geo.hpp"

#include <cmath>
#include <algorithm>

#include <GL/gl.h>

namespace cw::situation_view {

MapWindow SituationViewShell::tactical_map_window(int client_w, int client_h) const noexcept {
  const int cw = std::max(1, client_w);
  const int ch = std::max(1, client_h);
  if (view_mode_ == ViewMode::Split2dGlobe) {
    const int split_x = std::max(1, cw / 2);
    return MapWindow{0, 0, split_x, ch};
  }
  return MapWindow{0, 0, cw, ch};
}

MapWindow SituationViewShell::globe_map_window(int client_w, int client_h) const noexcept {
  const int cw = std::max(1, client_w);
  const int ch = std::max(1, client_h);
  if (view_mode_ == ViewMode::Split2dGlobe) {
    const int split_x = std::max(1, cw / 2);
    const int right_w = std::max(1, cw - split_x);
    return MapWindow{split_x, 0, right_w, ch};
  }
  return MapWindow{0, 0, cw, ch};
}

void SituationViewShell::reset_view_camera() noexcept {
  tactical_.reset_camera();
  drag_prev_valid_ = false;
  globe_.reset_content_orientation();
  picked_entity_id_.reset();
}

void SituationViewShell::process_entity_pick_mouse(const cw::engine::SituationPresentation& world, int client_w,
                                                   int client_h, int mouse_x, int mouse_y, bool left_down,
                                                   bool left_was_down) {
  if (left_down && !left_was_down) {
    pick_down_mx_ = mouse_x;
    pick_down_my_ = mouse_y;
    pick_drag_cancel_ = false;
  }
  if (left_down && left_was_down) {
    const int dx = mouse_x - pick_down_mx_;
    const int dy = mouse_y - pick_down_my_;
    if (dx * dx + dy * dy > 64) {
      pick_drag_cancel_ = true;
    }
  }
  if (!left_down && left_was_down && !pick_drag_cancel_) {
    if (auto id = try_pick_entity_at_screen(world, *this, client_w, client_h, mouse_x, mouse_y)) {
      picked_entity_id_ = std::move(id);
    } else {
      picked_entity_id_.reset();
    }
  }
}

void SituationViewShell::sync_globe_from_tactical_viewport(const cw::engine::SituationPresentation& world,
                                                           int client_w, int client_h,
                                                           int tactical_frustum_vp_w) noexcept {
  if (client_w < 1 || client_h < 1) {
    return;
  }
  const int tw = tactical_frustum_vp_w < 1 ? client_w : tactical_frustum_vp_w;
  globe_.clear_arcball_pending();
  cw::render::MercatorBounds b{};
  tactical_.expand_bounds_from_presentation(world, b);
  cw::render::MercatorOrthoFrustum tact{};
  tactical_.compute_interactive_frustum(b, tw, client_h, tact);
  const double tcx = static_cast<double>((tact.l + tact.r) * 0.5F);
  const double tcy = static_cast<double>((tact.b + tact.t) * 0.5F);
  double t_lon = 0.;
  double t_lat = 0.;
  cw::render::mercator_meters_to_lonlat(tcx, tcy, t_lon, t_lat);
  globe_.orient_content_to_place_lonlat_at_screen_center(t_lon, t_lat);
  constexpr double kPi = 3.14159265358979323846;
  const double lat_r = t_lat * (kPi / 180.0);
  const double ew =
      static_cast<double>(tact.r - tact.l) * std::max(1e-4, std::cos(lat_r));
  if (ew > 1.0) {
    globe_.set_camera_distance_for_visible_ew_meters(client_w, client_h, static_cast<double>(ew));
  }
}

void SituationViewShell::sync_tactical_from_globe_viewport(const cw::engine::SituationPresentation& world,
                                                           int client_w, int client_h,
                                                           int globe_vp_w_for_ew_readout) noexcept {
  if (client_w < 1 || client_h < 1) {
    return;
  }
  const int gw = globe_vp_w_for_ew_readout < 1 ? client_w : globe_vp_w_for_ew_readout;
  globe_.clear_arcball_pending();
  double g_lon = 0.;
  double g_lat = 0.;
  globe_.viewport_center_lonlat_from_pose(g_lon, g_lat);
  const double ew = globe_.visible_ground_ew_meters(gw, client_h);
  tactical_.set_frustum_center_lonlat(world, client_w, client_h, g_lon, g_lat);
  if (ew > 1.0) {
    tactical_.set_visible_ground_ew_meters_at_lat(world, client_w, client_h, ew, g_lat);
  }
}

void SituationViewShell::process_mouse_drag(cw::render::GlWindow& win,
                                            const cw::engine::SituationPresentation& world,
                                            bool& split_left_driven, bool& split_right_driven) {
  const int cw = win.client_width();
  const int ch = win.client_height();
  const MapWindow tact_win = tactical_map_window(cw, ch);
  const MapWindow glob_win = globe_map_window(cw, ch);

  if (win.left_button_down()) {
    if (drag_prev_valid_) {
      const int mx = win.mouse_client_x();
      const int my = win.mouse_client_y();
      /// 仅分屏时按窗口划分；纯 2D / 纯 3D 整窗即对应窗口。
      const bool split_left =
          (view_mode_ == ViewMode::Split2dGlobe) && tact_win.contains(drag_prev_mx_, drag_prev_my_);
      const bool split_right =
          (view_mode_ == ViewMode::Split2dGlobe) && glob_win.contains(drag_prev_mx_, drag_prev_my_);
      if (view_mode_ == ViewMode::Tactical2D || split_left) {
        const int dx = mx - drag_prev_mx_;
        const int dy = my - drag_prev_my_;
        tactical_.apply_mouse_pan_drag(world, tact_win.w, ch, dx, dy);
        if (view_mode_ == ViewMode::Split2dGlobe && (dx != 0 || dy != 0)) {
          split_left_driven = true;
        }
      } else if (view_mode_ == ViewMode::Globe3d || split_right) {
        const int pmx = drag_prev_mx_;
        const int pmy = drag_prev_my_;
        const int cmx = mx;
        const int cmy = my;
        if (pmx != cmx || pmy != cmy) {
          globe_.queue_arcball_drag(glob_win.to_local_x(pmx), glob_win.to_local_y(pmy), glob_win.to_local_x(cmx),
                                    glob_win.to_local_y(cmy));
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
  const int ch = win.client_height();
  const MapWindow tact_win = tactical_map_window(cw, ch);
  const MapWindow glob_win = globe_map_window(cw, ch);
  const int wd = win.consume_wheel_delta();
  if (wd != 0) {
    const int mx = win.mouse_client_x();
    const int my = win.mouse_client_y();
    if (view_mode_ == ViewMode::Split2dGlobe) {
      if (tact_win.contains(mx, my)) {
        split_left_driven = true;
      } else if (glob_win.contains(mx, my)) {
        split_right_driven = true;
      }
    }
    const bool wheel_on_globe =
        (view_mode_ == ViewMode::Globe3d) || (view_mode_ == ViewMode::Split2dGlobe && glob_win.contains(mx, my));
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

void SituationViewShell::pre_draw_split_sync(const cw::engine::SituationPresentation& world, int client_w,
                                             int client_h, bool split_left_driven, bool split_right_driven) {
  if (view_mode_ != ViewMode::Split2dGlobe) {
    split_matched_lonlat_grid_step_deg_ = 0.;
    return;
  }
  const MapWindow tact_win = tactical_map_window(client_w, client_h);
  const MapWindow glob_win = globe_map_window(client_w, client_h);
  const int split_x = tact_win.w;
  const int right_w = glob_win.w;
  if (globe_.arcball_pending()) {
    glViewport(glob_win.x, glob_win.y, glob_win.w, glob_win.h);
    globe_.setup_projection_and_modelview(right_w, client_h);
    glViewport(0, 0, client_w, client_h);
  }
  cw::render::MercatorBounds b{};
  tactical_.expand_bounds_from_presentation(world, b);
  cw::render::MercatorOrthoFrustum tact{};
  tactical_.compute_interactive_frustum(b, tact_win.w, tact_win.h, tact);
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
  auto sync_scale_right_to_left = [this, &world, split_x, client_h, g_lat, right_w]() {
    const double ew = globe_.visible_ground_ew_meters(right_w, client_h);
    if (ew > 1.0) {
      tactical_.set_visible_ground_ew_meters_at_lat(world, split_x, client_h, ew, g_lat);
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
    tactical_.set_frustum_center_lonlat(world, split_x, client_h, g_lon, g_lat);
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