#include "cw/render/tactical_map_2d.hpp"

#include "cw/render/mercator_geo.hpp"
#include "cw/scenario/parse.hpp"

#include <GL/gl.h>

#include <algorithm>
#include <cmath>

namespace cw::render {

void MercatorBounds::add_xy(float x, float y) {
  if (empty) {
    min_x = max_x = x;
    min_y = max_y = y;
    empty = false;
    return;
  }
  min_x = std::min(min_x, x);
  max_x = std::max(max_x, x);
  min_y = std::min(min_y, y);
  max_y = std::max(max_y, y);
}

void TacticalMercatorMap::reset_camera() noexcept { cam_ = ViewCamera{}; }

void TacticalMercatorMap::apply_wheel_zoom(int wheel_delta) noexcept {
  if (wheel_delta == 0) {
    return;
  }
  constexpr float kStep = 1.08F;
  const float s = static_cast<float>(wheel_delta) / 120.F;
  cam_.zoom *= std::pow(kStep, s);
  cam_.zoom = std::clamp(cam_.zoom, 0.0002F, 400.F);
}

void TacticalMercatorMap::expand_bounds_from_engine(const cw::engine::Engine& eng, MercatorBounds& b) const {
  const auto& snap = eng.situation();
  if (auto_bounds_include_entities_) {
    for (const auto& e : snap.entities) {
      b.add_xy(e.position.x, e.position.y);
      const float vx = e.velocity.x;
      const float vy = e.velocity.y;
      const float vm = std::sqrt(vx * vx + vy * vy);
      if (vm > 1e-3F) {
        b.add_xy(e.position.x + vx * 2.F, e.position.y + vy * 2.F);
      }
    }
  }
  for (const auto& r : eng.routes()) {
    for (const auto& w : r.waypoints) {
      b.add_xy(w.x, w.y);
    }
  }
  for (const auto& a : eng.airspaces()) {
    if (a.kind == cw::scenario::AirspaceKind::Box) {
      b.add_xy(a.box_min.x, a.box_min.y);
      b.add_xy(a.box_max.x, a.box_max.y);
    } else {
      for (const auto& p : a.polygon) {
        b.add_xy(p.x, p.y);
      }
    }
  }
  if (b.empty) {
    const float H = kMercatorHalfExtentM;
    b.add_xy(-H, -H);
    b.add_xy(H, H);
  }
}

void TacticalMercatorMap::compute_ortho_frustum(const MercatorBounds& b, int vp_w, int vp_h,
                                                  MercatorOrthoFrustum& f) const {
  const float cx = (b.min_x + b.max_x) * 0.5F;
  const float cy = (b.min_y + b.max_y) * 0.5F;
  float hw = (b.max_x - b.min_x) * 0.5F;
  float hh = (b.max_y - b.min_y) * 0.5F;
  const float pad = std::max(hw, hh) * 0.12F + 50.F;
  hw += pad;
  hh += pad;
  if (hw < 200.F) {
    hw = 200.F;
  }
  if (hh < 200.F) {
    hh = 200.F;
  }
  const float ax = static_cast<float>(vp_w) / static_cast<float>(vp_h);
  if (hw / hh > ax) {
    hh = hw / ax;
  } else {
    hw = hh * ax;
  }
  f.l = cx - hw;
  f.r = cx + hw;
  f.b = cy - hh;
  f.t = cy + hh;
}

void TacticalMercatorMap::compute_interactive_frustum(const MercatorBounds& b, int vp_w, int vp_h,
                                                      MercatorOrthoFrustum& tactical) {
  MercatorOrthoFrustum fit{};
  compute_ortho_frustum(b, vp_w, vp_h, fit);
  const float cx_base = (fit.l + fit.r) * 0.5F;
  const float cy_base = (fit.b + fit.t) * 0.5F;
  const float cx = cx_base + cam_.pan_mx;
  float cy = cy_base + cam_.pan_my;
  float hw = (fit.r - fit.l) * 0.5F / cam_.zoom;
  float hh = (fit.t - fit.b) * 0.5F / cam_.zoom;
  const float ax = static_cast<float>(vp_w) / static_cast<float>(vp_h);
  if (hw > ax * hh) {
    hh = hw / ax;
  } else {
    hw = hh * ax;
  }

  const float cap_hw = (ax >= 1.F) ? (kMercatorHalfExtentM * ax) : kMercatorHalfExtentM;
  const float cap_hh = (ax >= 1.F) ? kMercatorHalfExtentM : (kMercatorHalfExtentM / ax);
  float s_cap = 1.F;
  if (hw > cap_hw && cap_hw > 1.F) {
    s_cap = std::min(s_cap, cap_hw / hw);
  }
  if (hh > cap_hh && cap_hh > 1.F) {
    s_cap = std::min(s_cap, cap_hh / hh);
  }
  hw *= s_cap;
  hh *= s_cap;

  const float y_max = kMercatorHalfExtentM;
  const float cy_min = -y_max + hh;
  const float cy_max = y_max - hh;
  if (cy_min <= cy_max) {
    cy = std::clamp(cy, cy_min, cy_max);
  } else {
    cy = 0.F;
  }
  cam_.pan_my = cy - cy_base;

  tactical.l = cx - hw;
  tactical.r = cx + hw;
  tactical.b = cy - hh;
  tactical.t = cy + hh;
}

void TacticalMercatorMap::apply_mouse_pan_drag(const cw::engine::Engine& eng, int vp_w, int vp_h, int dx_win,
                                                 int dy_win) {
  if (dx_win == 0 && dy_win == 0) {
    return;
  }
  MercatorBounds b{};
  expand_bounds_from_engine(eng, b);
  MercatorOrthoFrustum tactical{};
  compute_interactive_frustum(b, vp_w, vp_h, tactical);
  const float wx = tactical.r - tactical.l;
  const float wy = tactical.t - tactical.b;
  const float mpx = wx / static_cast<float>(std::max(1, vp_w));
  const float mpy = wy / static_cast<float>(std::max(1, vp_h));
  cam_.pan_mx -= static_cast<float>(dx_win) * mpx;
  cam_.pan_my += static_cast<float>(dy_win) * mpy;
}

void TacticalMercatorMap::set_frustum_center_lonlat(const cw::engine::Engine& eng, int vp_w, int vp_h,
                                                    double lon_deg, double lat_deg) {
  MercatorBounds b{};
  expand_bounds_from_engine(eng, b);
  MercatorOrthoFrustum fit{};
  compute_ortho_frustum(b, vp_w, vp_h, fit);
  const double cx_base = static_cast<double>((fit.l + fit.r) * 0.5F);
  const double cy_base = static_cast<double>((fit.b + fit.t) * 0.5F);
  double mx = 0.;
  double my = 0.;
  lonlat_deg_to_mercator_meters(lon_deg, lat_deg, mx, my);
  double raw_pan_x = mx - cx_base;
  const double W = static_cast<double>(kWorldWidthM);
  while (raw_pan_x > W * 0.5) {
    raw_pan_x -= W;
  }
  while (raw_pan_x < -W * 0.5) {
    raw_pan_x += W;
  }
  cam_.pan_mx = static_cast<float>(raw_pan_x);
  cam_.pan_my = static_cast<float>(my - cy_base);
  MercatorOrthoFrustum dummy{};
  compute_interactive_frustum(b, vp_w, vp_h, dummy);
}

void TacticalMercatorMap::set_visible_ground_ew_meters_at_lat(const cw::engine::Engine& eng, int vp_w,
                                                              int vp_h, double physical_ew_m,
                                                              double center_lat_deg) {
  if (physical_ew_m < 1.0 || vp_w < 1 || vp_h < 1) {
    return;
  }
  constexpr double kPi = 3.14159265358979323846;
  const double lat_r = center_lat_deg * (kPi / 180.0);
  const double cos_lat = std::max(1e-4, std::cos(lat_r));
  const double target_tw = physical_ew_m / cos_lat;

  MercatorBounds b{};
  expand_bounds_from_engine(eng, b);
  MercatorOrthoFrustum fit{};
  compute_ortho_frustum(b, vp_w, vp_h, fit);
  const float fit_w = fit.r - fit.l;
  if (fit_w < 1e-3F) {
    return;
  }

  for (int iter = 0; iter < 8; ++iter) {
    cam_.zoom = static_cast<float>(static_cast<double>(fit_w) / target_tw);
    cam_.zoom = std::clamp(cam_.zoom, 0.0002F, 400.F);
    MercatorOrthoFrustum tactical{};
    compute_interactive_frustum(b, vp_w, vp_h, tactical);
    const double tw = static_cast<double>(tactical.r - tactical.l);
    const double ew = tw * cos_lat;
    const double err = ew / physical_ew_m;
    if (std::abs(err - 1.0) < 0.005) {
      break;
    }
    cam_.zoom = static_cast<float>(static_cast<double>(cam_.zoom) / err);
  }
}

void TacticalMercatorMap::expand_frustum_for_world_basemap(const MercatorOrthoFrustum& tactical,
                                                           MercatorOrthoFrustum& map) {
  constexpr float kMinHalfSpanM = 4.0e6F;
  const float cx = (tactical.l + tactical.r) * 0.5F;
  const float cy = (tactical.b + tactical.t) * 0.5F;
  const float tw = tactical.r - tactical.l;
  const float th = tactical.t - tactical.b;
  const float tactical_ax = (th > 1e-3F) ? (tw / th) : 1.F;

  float hw = std::max(tw * 0.5F, kMinHalfSpanM);
  float hh = std::max(th * 0.5F, kMinHalfSpanM);
  if (hw > tactical_ax * hh) {
    hh = hw / tactical_ax;
  } else {
    hw = hh * tactical_ax;
  }
  if (hw < kMinHalfSpanM) {
    const float s = kMinHalfSpanM / hw;
    hw *= s;
    hh *= s;
  }
  if (hh < kMinHalfSpanM) {
    const float s = kMinHalfSpanM / hh;
    hw *= s;
    hh *= s;
  }

  map.l = cx - hw;
  map.r = cx + hw;
  map.b = cy - hh;
  map.t = cy + hh;
}

void TacticalMercatorMap::apply_ortho_frustum(const MercatorOrthoFrustum& f) {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(static_cast<double>(f.l), static_cast<double>(f.r), static_cast<double>(f.b),
          static_cast<double>(f.t), -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

void TacticalMercatorMap::mercator_meters_to_tex_uv(double x, double y, float& u, float& v) {
  constexpr double kR = 6378137.0;
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kYMax = 20037508.34;
  const double cy = std::max(-kYMax, std::min(kYMax, y));
  const double x_lon = cw::render::wrap_mercator_lon_x(x);
  const double lon = x_lon / kR;
  const double lat = 2.0 * std::atan(std::exp(cy / kR)) - kPi * 0.5;
  const double lon_deg = lon * (180.0 / kPi);
  const double lat_deg = lat * (180.0 / kPi);
  u = static_cast<float>((lon_deg + 180.0) / 360.0);
  v = static_cast<float>((90.0 - lat_deg) / 180.0);
  u = std::max(0.F, std::min(1.F, u));
  v = std::max(0.F, std::min(1.F, v));
}

void TacticalMercatorMap::window_pixel_to_lonlat(int mx, int my, int vp_w, int vp_h,
                                                 const MercatorOrthoFrustum& tactical, double& lon_deg,
                                                 double& lat_deg) {
  const int w = std::max(1, vp_w);
  const int h = std::max(1, vp_h);
  const int px = std::clamp(mx, 0, w - 1);
  const int py = std::clamp(my, 0, h - 1);
  const double fx = (static_cast<double>(px) + 0.5) / static_cast<double>(w);
  const double fy = (static_cast<double>(h - 1 - py) + 0.5) / static_cast<double>(h);
  const double wx =
      static_cast<double>(tactical.l) + fx * (static_cast<double>(tactical.r) - static_cast<double>(tactical.l));
  const double wy =
      static_cast<double>(tactical.b) + fy * (static_cast<double>(tactical.t) - static_cast<double>(tactical.b));
  mercator_meters_to_lonlat(wx, wy, lon_deg, lat_deg);
}

float TacticalMercatorMap::mercator_periodic_x(float x, float cx_ref) noexcept {
  const float W = kWorldWidthM;
  float d = x - cx_ref;
  d -= std::round(d / W) * W;
  return cx_ref + d;
}

}  // namespace cw::render
