#include "cw/render/lonlat_grid.hpp"

#include "cw/render/globe_pixel_scale.hpp"
#include "cw/render/mercator_geo.hpp"
#include "cw/render/tactical_map_2d.hpp"

#include <GL/gl.h>
#include <GL/glu.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace cw::render {

namespace {

constexpr double kPi = 3.14159265358979323846;

bool try_project_label_anchor(double wx, double wy, double wz, int vp_w, int vp_h, double& sx,
                              double& sy, double& sz) noexcept {
  (void)vp_w;
  (void)vp_h;
  GLint view[4]{};
  GLdouble model[16]{};
  GLdouble proj[16]{};
  glGetIntegerv(GL_VIEWPORT, view);
  glGetDoublev(GL_MODELVIEW_MATRIX, model);
  glGetDoublev(GL_PROJECTION_MATRIX, proj);
  if (gluProject(wx, wy, wz, model, proj, view, &sx, &sy, &sz) != GL_TRUE) {
    return false;
  }
  if (!std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(sz)) {
    return false;
  }
  /// `gluProject` 为整窗坐标；分屏时 `GL_VIEWPORT.x` 非 0，不得以 `vp_w/vp_h` 当 0 原点视口。
  const double vx0 = static_cast<double>(view[0]);
  const double vy0 = static_cast<double>(view[1]);
  const double vww = static_cast<double>(std::max(1, view[2]));
  const double vwh = static_cast<double>(std::max(1, view[3]));
  const double margin = 22.0;
  if (sx < vx0 + margin || sy < vy0 + margin || sx > vx0 + vww - margin ||
      sy > vy0 + vwh - margin) {
    return false;
  }
  return true;
}

bool point_in_tactical_view(double mx, double my, const MercatorOrthoFrustum& t,
                            float cx_ref) noexcept {
  const float px = TacticalMercatorMap::mercator_periodic_x(static_cast<float>(mx), cx_ref);
  const float myf = static_cast<float>(my);
  return px >= t.l && px <= t.r && myf >= t.b && myf <= t.t;
}

}  // namespace

double visible_sphere_diameter_deg(float camera_distance) noexcept {
  const double d = std::max(1.001, static_cast<double>(camera_distance));
  const double half = std::asin(1.0 / d);
  return 2.0 * half * (180.0 / kPi);
}

double pick_lonlat_step_deg(double visible_diameter_deg, float camera_distance) noexcept {
  const double d = std::max(1.001, static_cast<double>(camera_distance));
  const double h = std::max(0.0, d - 1.0);
  const double ideal_span = std::max(0.05, visible_diameter_deg / 9.0);
  const double ideal_h = std::max(0.008, 14.0 * h);
  double ideal;
  if (h < 2.0) {
    ideal = std::min(ideal_span, ideal_h);
  } else {
    ideal = std::max(10.0, visible_diameter_deg / 3.4);
  }
  const double cand[] = {0.05, 0.1, 0.25, 0.5, 1.0,  2.0,  3.0,  5.0,  10.0,
                         15.0, 20.0, 30.0, 45.0, 90.0};
  double step = 90.0;
  for (double c : cand) {
    if (c >= ideal * 0.62) {
      step = c;
      break;
    }
  }
  int max_mer_lines = 56;
  int max_par_lines = 28;
  if (h < 0.02) {
    max_mer_lines = 36000;
    max_par_lines = 18000;
  } else if (h < 0.35) {
    max_mer_lines = 900;
    max_par_lines = 450;
  } else if (h < 1.2) {
    max_mer_lines = 360;
    max_par_lines = 180;
  } else if (h < 4.0) {
    max_mer_lines = 180;
    max_par_lines = 90;
  }
  /// 墨卡托等视图 `camera_distance` 映射下 `h` 可能不够小；按视场角跨度再放宽上限，使最细可到0.05°。
  const double span_v = visible_diameter_deg;
  if (span_v < 0.35) {
    max_mer_lines = std::max(max_mer_lines, 36000);
    max_par_lines = std::max(max_par_lines, 18000);
  } else if (span_v < 4.0) {
    max_mer_lines = std::max(max_mer_lines, 7200);
    max_par_lines = std::max(max_par_lines, 3600);
  }
  while (360.0 / step > static_cast<double>(max_mer_lines)) {
    step *= 2.0;
    if (step > 180.0) {
      step = 180.0;
      break;
    }
  }
  while (180.0 / step > static_cast<double>(max_par_lines)) {
    step *= 2.0;
    if (step > 90.0) {
      step = 90.0;
      break;
    }
  }
  return step;
}

float equiv_camera_distance_from_span_deg(double span_deg) noexcept {
  double span = std::clamp(span_deg, 0.05, 179.0);
  const double half_rad = (0.5 * span) * (kPi / 180.0);
  double s = std::sin(half_rad);
  if (s < 1e-9) {
    s = 1e-9;
  }
  return static_cast<float>(1.0 / s);
}

float tactical_equiv_camera_distance_from_span_deg(double span_deg) noexcept {
  const double s = std::clamp(span_deg, 1.0, 360.0);
  const double u = std::min(s, 180.0) / 180.0;
  return 1.0F + 15.0F * static_cast<float>(u);
}

double tactical_frustum_lonlat_span_deg(const MercatorOrthoFrustum& tactical, float cx_ref) noexcept {
  const double cy = (static_cast<double>(tactical.b) + static_cast<double>(tactical.t)) * 0.5;
  const double cxm = static_cast<double>(cx_ref);
  double lon_l = 0.;
  double lon_r = 0.;
  double lat_l = 0.;
  double lat_r = 0.;
  double lat_t = 0.;
  double lat_b = 0.;
  mercator_meters_to_lonlat(static_cast<double>(tactical.l), cy, lon_l, lat_l);
  mercator_meters_to_lonlat(static_cast<double>(tactical.r), cy, lon_r, lat_r);
  double dlon = lon_r - lon_l;
  while (dlon > 180.0) {
    dlon -= 360.0;
  }
  while (dlon < -180.0) {
    dlon += 360.0;
  }
  const double span_lon = std::fabs(dlon);
  mercator_meters_to_lonlat(cxm, static_cast<double>(tactical.t), lon_r, lat_t);
  mercator_meters_to_lonlat(cxm, static_cast<double>(tactical.b), lon_l, lat_b);
  const double span_lat = std::fabs(lat_t - lat_b);
  return std::max(span_lon, span_lat);
}

void fmt_lon(char* buf, size_t cap, double lon_deg, double step_deg) noexcept {
  double L = lon_deg;
  while (L < -180.0) {
    L += 360.0;
  }
  while (L > 180.0) {
    L -= 360.0;
  }
  const double a = std::fabs(L);
  const char hemi = L >= 0.0 ? 'e' : 'w';
  if (step_deg >= 0.999) {
    const int ai = static_cast<int>(std::lround(a));
    if (ai < 10) {
      std::snprintf(buf, cap, "%02d%c", ai, hemi);
    } else {
      std::snprintf(buf, cap, "%d%c", ai, hemi);
    }
  } else if (step_deg >= 0.1) {
    std::snprintf(buf, cap, "%.1f%c", a, hemi);
  } else {
    std::snprintf(buf, cap, "%.2f%c", a, hemi);
  }
}

void fmt_lat(char* buf, size_t cap, double lat_deg, double step_deg) noexcept {
  const double a = std::fabs(lat_deg);
  const char hemi = lat_deg >= 0.0 ? 'n' : 's';
  if (step_deg >= 0.999) {
    const int ai = static_cast<int>(std::lround(a));
    if (ai < 10) {
      std::snprintf(buf, cap, "%02d%c", ai, hemi);
    } else {
      std::snprintf(buf, cap, "%d%c", ai, hemi);
    }
  } else if (step_deg >= 0.1) {
    std::snprintf(buf, cap, "%.1f%c", a, hemi);
  } else {
    std::snprintf(buf, cap, "%.2f%c", a, hemi);
  }
}

bool append_lonlat_grid_label(std::vector<GlobeLonLatLabel>* out, int vp_w, int vp_h, float pixel_scale,
                              double wx, double wy, double wz, float ox, float oy,
                              const char* text) noexcept {
  if (out == nullptr || text == nullptr || text[0] == '\0') {
    return false;
  }
  double sx = 0.;
  double sy = 0.;
  double sz = 0.;
  if (!try_project_label_anchor(wx, wy, wz, vp_w, vp_h, sx, sy, sz)) {
    return false;
  }
  GlobeLonLatLabel L{};
  L.sx = static_cast<float>(sx);
  L.sy = static_cast<float>(sy);
  L.ox = ox;
  L.oy = oy;
  L.pixel_scale = pixel_scale;
  std::strncpy(L.text, text, sizeof(L.text) - 1);
  L.text[sizeof(L.text) - 1] = '\0';
  out->push_back(L);
  return true;
}

void draw_tactical_lonlat_grid(int vp_w, int vp_h, const MercatorOrthoFrustum& tactical, float cx_ref,
                               std::vector<GlobeLonLatLabel>* labels_out, bool viewport_center_valid,
                               double viewport_center_lon_deg, double viewport_center_lat_deg) noexcept {
  if (vp_w < 2 || vp_h < 2) {
    return;
  }
  if (labels_out != nullptr) {
    labels_out->clear();
  }

  const double span = tactical_frustum_lonlat_span_deg(tactical, cx_ref);
  const float equiv_d = tactical_equiv_camera_distance_from_span_deg(span);
  const double step = pick_lonlat_step_deg(span, equiv_d);

  const float ref_d = 3.2F;
  float pixel_scale = static_cast<float>(ref_d / std::max(1.001F, equiv_d));
  pixel_scale *= std::sqrt(static_cast<float>(std::max(360, vp_h)) / 720.F);
  pixel_scale = clamp_globe_label_pixel_scale(pixel_scale);

  constexpr double kEarthR = 6378137.0;
  const int n_mer = static_cast<int>(std::lround(360.0 / step));
  const int k0 = static_cast<int>(std::floor(-90.0 / step + 1e-9));
  const int k1 = static_cast<int>(std::ceil(90.0 / step - 1e-9));
  int n_par_draw = 0;
  for (int k = k0; k <= k1; ++k) {
    const double lat_x = static_cast<double>(k) * step;
    if (lat_x <= -90.0 + 1e-6 || lat_x >= 90.0 - 1e-6) {
      continue;
    }
    n_par_draw++;
  }

  constexpr double kTargetMeridianLabels = 9.0;
  constexpr double kTargetParallelLabels = 8.0;
  constexpr double kPolarLatCapDeg = 76.0;
  /// 与球面注记公式一致时视场角直径 ≤180°；墨卡托经向跨度可达 360°，应用 `min(span,180)` 避免 mer_w→1 导致注记过密。
  const double span_lbl = std::min(span, 180.0);
  const double mer_w = std::max(1.0, 360.0 - span_lbl);
  const double par_w = std::max(1.0, 180.0 - span_lbl);
  int lbl_mer = std::max(1, static_cast<int>(std::ceil(n_mer * mer_w / (360.0 * kTargetMeridianLabels))));
  int lbl_par =
      std::max(1, static_cast<int>(std::ceil(std::max(1, n_par_draw) * par_w / (180.0 * kTargetParallelLabels))));
  {
    const long n_i = (static_cast<long>(n_mer) + lbl_mer - 1) / lbl_mer;
    const long n_k = k1 >= k0 ? (static_cast<long>(k1 - k0) / lbl_par + 1L) : 0L;
    long approx_pairs = n_i * n_k;
    while (approx_pairs > 480L && lbl_mer < n_mer) {
      lbl_mer *= 2;
      lbl_par *= 2;
      const long n_i2 = (static_cast<long>(n_mer) + lbl_mer - 1) / lbl_mer;
      const long n_k2 = k1 >= k0 ? (static_cast<long>(k1 - k0) / lbl_par + 1L) : 0L;
      approx_pairs = n_i2 * n_k2;
    }
  }

  int mer_label_i0 = 0;
  int par_label_k0 = k0;
  if (viewport_center_valid && n_mer > 0 && lbl_mer > 0 && lbl_par > 0) {
    const double dnm = static_cast<double>(n_mer);
    int i_c = static_cast<int>(std::lround((viewport_center_lon_deg + 180.0) * dnm / 360.0));
    i_c = std::clamp(i_c, 0, n_mer - 1);
    mer_label_i0 = (i_c % lbl_mer + lbl_mer) % lbl_mer;
    int k_c = static_cast<int>(std::lround(viewport_center_lat_deg / step));
    k_c = std::clamp(k_c, k0, k1);
    const int k_off = ((k_c - k0) % lbl_par + lbl_par) % lbl_par;
    par_label_k0 = k0 + k_off;
  }

  const float lat_off_px = 6.8F * pixel_scale;
  const float lon_off_px = 6.8F * pixel_scale;

  glDisable(GL_TEXTURE_2D);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glLineWidth(1.0F);
  glColor4f(0.78F, 0.82F, 0.86F, 0.52F);

  for (int i = 0; i < n_mer; ++i) {
    const double lon_deg = -180.0 + static_cast<double>(i) * (360.0 / static_cast<double>(n_mer));
    const bool emphasis =
        std::fabs(lon_deg) < 1e-6 || std::fabs(std::fabs(lon_deg) - 180.0) < 1e-6;
    if (emphasis) {
      glLineWidth(1.2F);
      glColor4f(0.88F, 0.9F, 0.93F, 0.62F);
    }
    const double lonr = lon_deg * (kPi / 180.0);
    const float xm =
        TacticalMercatorMap::mercator_periodic_x(static_cast<float>(kEarthR * lonr), cx_ref);
    if (xm >= tactical.l && xm <= tactical.r) {
      glBegin(GL_LINES);
      glVertex2f(xm, tactical.b);
      glVertex2f(xm, tactical.t);
      glEnd();
    }
    if (emphasis) {
      glLineWidth(1.0F);
      glColor4f(0.78F, 0.82F, 0.86F, 0.52F);
    }
  }

  for (int k = k0; k <= k1; ++k) {
    const double lat_deg = static_cast<double>(k) * step;
    if (lat_deg <= -90.0 + 1e-6 || lat_deg >= 90.0 - 1e-6) {
      continue;
    }
    const bool emphasis = std::fabs(lat_deg) < 1e-6;
    if (emphasis) {
      glLineWidth(1.2F);
      glColor4f(0.88F, 0.9F, 0.93F, 0.62F);
    }
    double mx = 0.;
    double my = 0.;
    lonlat_deg_to_mercator_meters(0.0, lat_deg, mx, my);
    const float yf = static_cast<float>(my);
    if (yf >= tactical.b && yf <= tactical.t) {
      glBegin(GL_LINES);
      glVertex2f(tactical.l, yf);
      glVertex2f(tactical.r, yf);
      glEnd();
    }
    if (emphasis) {
      glLineWidth(1.0F);
      glColor4f(0.78F, 0.82F, 0.86F, 0.52F);
    }
  }

  if (labels_out != nullptr) {
    const float lat_ox = 0.F;
    const float lat_oy = lat_off_px;
    const float lon_ox = lon_off_px;
    const float lon_oy = 0.F;

    auto emit_label_2d = [&](double mx, double my, float ox, float oy, const char* text) {
      if (text == nullptr || text[0] == '\0') {
        return;
      }
      if (!point_in_tactical_view(mx, my, tactical, cx_ref)) {
        return;
      }
      append_lonlat_grid_label(labels_out, vp_w, vp_h, pixel_scale, mx, my, 0.0, ox, oy, text);
    };

    /// 与三维十字注记一致：仅中心格显示「纬 经」；过视口中心纬线只标经度、过中心经线只标纬度。
    /// 两臂须遍历全部经/纬索引，再用与绘线相同的视锥条件过滤；勿用 `lbl_mer`/`lbl_par` 步进，否则放大后许多已画出的网格线无注记。
    const bool use_crosshair_labels = viewport_center_valid && n_mer > 0;

    if (use_crosshair_labels) {
      const double dnm = static_cast<double>(n_mer);
      int i_center = static_cast<int>(std::lround((viewport_center_lon_deg + 180.0) * dnm / 360.0));
      i_center = std::clamp(i_center, 0, n_mer - 1);
      int k_center = static_cast<int>(std::lround(viewport_center_lat_deg / step));
      k_center = std::clamp(k_center, k0, k1);

      const double lon_c = -180.0 + static_cast<double>(i_center) * (360.0 / dnm);
      const double lat_c = static_cast<double>(k_center) * step;
      if (lat_c > -90.0 + 1e-6 && lat_c < 90.0 - 1e-6 && std::fabs(lat_c) <= kPolarLatCapDeg) {
        double mxc = 0.;
        double myc = 0.;
        lonlat_deg_to_mercator_meters(lon_c, lat_c, mxc, myc);
        mxc = static_cast<double>(
            TacticalMercatorMap::mercator_periodic_x(static_cast<float>(mxc), cx_ref));
        char lonbuf_c[20]{};
        char latbuf_c[20]{};
        char comb[56]{};
        fmt_lon(lonbuf_c, sizeof(lonbuf_c), lon_c, step);
        fmt_lat(latbuf_c, sizeof(latbuf_c), lat_c, step);
        std::snprintf(comb, sizeof(comb), "%s %s", latbuf_c, lonbuf_c);
        emit_label_2d(mxc, myc, 0.F, 0.F, comb);
      }

      const double lat_deg_parallel = static_cast<double>(k_center) * step;
      if (lat_deg_parallel > -90.0 + 1e-6 && lat_deg_parallel < 90.0 - 1e-6) {
        for (int i = 0; i < n_mer; ++i) {
          if (i == i_center) {
            continue;
          }
          const double lon_deg = -180.0 + static_cast<double>(i) * (360.0 / dnm);
          const double lonr = lon_deg * (kPi / 180.0);
          const float xm =
              TacticalMercatorMap::mercator_periodic_x(static_cast<float>(kEarthR * lonr), cx_ref);
          if (xm < tactical.l || xm > tactical.r) {
            continue;
          }
          double mx = 0.;
          double my = 0.;
          lonlat_deg_to_mercator_meters(lon_deg, lat_deg_parallel, mx, my);
          mx = static_cast<double>(TacticalMercatorMap::mercator_periodic_x(static_cast<float>(mx), cx_ref));
          char lonbuf[20]{};
          fmt_lon(lonbuf, sizeof(lonbuf), lon_deg, step);
          emit_label_2d(mx, my, lon_ox, lon_oy, lonbuf);
        }
      }

      const double lon_deg_mer = -180.0 + static_cast<double>(i_center) * (360.0 / dnm);
      const double lonr_mer = lon_deg_mer * (kPi / 180.0);
      const float xm_mer =
          TacticalMercatorMap::mercator_periodic_x(static_cast<float>(kEarthR * lonr_mer), cx_ref);
      if (xm_mer >= tactical.l && xm_mer <= tactical.r) {
        for (int kk = k0; kk <= k1; ++kk) {
          if (kk == k_center) {
            continue;
          }
          const double lat_deg = static_cast<double>(kk) * step;
          if (lat_deg <= -90.0 + 1e-6 || lat_deg >= 90.0 - 1e-6) {
            continue;
          }
          if (std::fabs(lat_deg) > kPolarLatCapDeg) {
            continue;
          }
          double mx = 0.;
          double my = 0.;
          lonlat_deg_to_mercator_meters(lon_deg_mer, lat_deg, mx, my);
          mx = static_cast<double>(TacticalMercatorMap::mercator_periodic_x(static_cast<float>(mx), cx_ref));
          const float yf = static_cast<float>(my);
          if (yf < tactical.b || yf > tactical.t) {
            continue;
          }
          char latbuf[20]{};
          fmt_lat(latbuf, sizeof(latbuf), lat_deg, step);
          emit_label_2d(mx, my, lat_ox, lat_oy, latbuf);
        }
      }
    } else {
      for (int i = mer_label_i0; i < n_mer; i += lbl_mer) {
        const double lon_deg = -180.0 + static_cast<double>(i) * (360.0 / static_cast<double>(n_mer));
        for (int kk = par_label_k0; kk <= k1; kk += lbl_par) {
          const double lat_deg = static_cast<double>(kk) * step;
          if (lat_deg <= -90.0 + 1e-6 || lat_deg >= 90.0 - 1e-6) {
            continue;
          }
          if (std::fabs(lat_deg) > kPolarLatCapDeg) {
            continue;
          }
          double mx = 0.;
          double my = 0.;
          lonlat_deg_to_mercator_meters(lon_deg, lat_deg, mx, my);
          mx = static_cast<double>(TacticalMercatorMap::mercator_periodic_x(static_cast<float>(mx), cx_ref));
          if (!point_in_tactical_view(mx, my, tactical, cx_ref)) {
            continue;
          }
          char lonbuf[20]{};
          char latbuf[20]{};
          fmt_lon(lonbuf, sizeof(lonbuf), lon_deg, step);
          fmt_lat(latbuf, sizeof(latbuf), lat_deg, step);
          append_lonlat_grid_label(labels_out, vp_w, vp_h, pixel_scale, mx, my, 0.0, lat_ox, lat_oy, latbuf);
          append_lonlat_grid_label(labels_out, vp_w, vp_h, pixel_scale, mx, my, 0.0, lon_ox, lon_oy, lonbuf);
        }
      }
    }
  }

  glDisable(GL_BLEND);
  glLineWidth(1.F);
  glColor4f(1.F, 1.F, 1.F, 1.F);
}

}  // namespace cw::render
