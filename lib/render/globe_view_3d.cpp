#include "cw/render/globe_view_3d.hpp"

#include "cw/render/lonlat_grid.hpp"
#include "cw/render/mercator_geo.hpp"

#include "cw/camera_basis.hpp"
#include "cw/mat4.hpp"
#include "cw/quat.hpp"
#include "cw/rot_align.hpp"

#include <GL/gl.h>
#include <GL/glu.h>

#ifndef GL_POLYGON_OFFSET_LINE
#define GL_POLYGON_OFFSET_LINE 0x2A02
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace cw::render {

namespace {

/// 左乘绕地心→相机轴 `normalize(eye)` 的滚转，使地理北 `R*(0,1,0)` 在垂直于视线的平面内与 `gluLookAt` 相机上对齐。
void north_roll_align_content_R(double R[16], double ex, double ey, double ez) {
  const double elen = std::sqrt(ex * ex + ey * ey + ez * ez);
  if (elen < 1e-15) {
    return;
  }
  const double vx = ex / elen;
  const double vy = ey / elen;
  const double vz = ez / elen;
  double nx = R[4];
  double ny = R[5];
  double nz = R[6];
  const double ndotv = nx * vx + ny * vy + nz * vz;
  double npx = nx - ndotv * vx;
  double npy = ny - ndotv * vy;
  double npz = nz - ndotv * vz;
  const double npl = std::sqrt(npx * npx + npy * npy + npz * npz);
  if (npl < 1e-12) {
    return;
  }
  npx /= npl;
  npy /= npl;
  npz /= npl;
  double u_cam[3]{};
  cw::math::glu_lookat_camera_up_world(ex, ey, ez, u_cam);
  const double cx = npy * u_cam[2] - npz * u_cam[1];
  const double cy = npz * u_cam[0] - npx * u_cam[2];
  const double cz = npx * u_cam[1] - npy * u_cam[0];
  const double triple = cx * vx + cy * vy + cz * vz;
  const double dotpu = npx * u_cam[0] + npy * u_cam[1] + npz * u_cam[2];
  const double theta = std::atan2(triple, dotpu);
  cw::math::Quat q_roll{};
  const double axis[3] = {vx, vy, vz};
  cw::math::quat_from_axis_angle_unit(axis, theta, q_roll);
  double R_roll[16]{};
  cw::math::quat_to_mat4_col(q_roll, R_roll);
  double R_out[16]{};
  cw::math::mat4_mul_col_major(R_roll, R, R_out);
  for (int i = 0; i < 16; ++i) {
    R[i] = R_out[i];
  }
}

/// 与 `situation_view` HUD 地面宽度一致：单位球半径 1、`h_eye = d - 1`，估算视口中心附近地面张角的较大边（度）。
double globe_patch_diameter_deg(int vp_w, int vp_h, float camera_distance) noexcept {
  constexpr double kEarthR = 6378137.0;
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kFovYDeg = 50.0;
  const double d = std::max(1.001, static_cast<double>(camera_distance));
  const double h_eye = std::max(0.0, d - 1.0);
  const double tan_half = std::tan(0.5 * kFovYDeg * (kPi / 180.0));
  const double aspect =
      static_cast<double>(std::max(1, vp_w)) / static_cast<double>(std::max(1, vp_h));
  const double ground_w_y_m = 2.0 * h_eye * tan_half * kEarthR;
  const double ground_w_x_m = ground_w_y_m * aspect;
  const double ax = (ground_w_x_m / kEarthR) * (180.0 / kPi);
  const double ay = (ground_w_y_m / kEarthR) * (180.0 / kPi);
  return std::max(1e-4, std::max(ax, ay));
}

}  // namespace

namespace grid_ns {

bool surface_point_faces_camera(const double R[16], double px, double py, double pz, double eye_x,
                                double eye_y, double eye_z) {
  const double wx = R[0] * px + R[4] * py + R[8] * pz;
  const double wy = R[1] * px + R[5] * py + R[9] * pz;
  const double wz = R[2] * px + R[6] * py + R[10] * pz;
  const double vx = eye_x - wx;
  const double vy = eye_y - wy;
  const double vz = eye_z - wz;
  return (wx * vx + wy * vy + wz * vz) > 1e-8;
}

/// 表面法向与「指向相机」方向夹角余弦；过小表示贴近球体轮廓（掠射），注记会挤在天际线一带。
bool surface_point_label_view_ok(const double R[16], double px, double py, double pz, double eye_x,
                                 double eye_y, double eye_z, double min_cos) {
  const double wx = R[0] * px + R[4] * py + R[8] * pz;
  const double wy = R[1] * px + R[5] * py + R[9] * pz;
  const double wz = R[2] * px + R[6] * py + R[10] * pz;
  const double vx = eye_x - wx;
  const double vy = eye_y - wy;
  const double vz = eye_z - wz;
  const double plen = std::sqrt(wx * wx + wy * wy + wz * wz);
  const double vlen = std::sqrt(vx * vx + vy * vy + vz * vz);
  if (plen < 1e-12 || vlen < 1e-12) {
    return false;
  }
  const double c = (wx * vx + wy * vy + wz * vz) / (plen * vlen);
  return c >= min_cos;
}

}  // namespace grid_ns

void draw_globe_lonlat_grid(int vp_w, int vp_h, float camera_distance, const double content_R[16],
                            double eye_x, double eye_y, double eye_z, double radius,
                            std::vector<GlobeLonLatLabel>* labels_out, bool viewport_center_valid,
                            double viewport_center_lon_deg, double viewport_center_lat_deg) noexcept {
  if (vp_w < 2 || vp_h < 2 || !(radius > 0.0)) {
    return;
  }
  if (labels_out != nullptr) {
    labels_out->clear();
  }

  const double span = visible_sphere_diameter_deg(camera_distance);
  const double step = pick_lonlat_step_deg(span, camera_distance);

  const float ref_d = 3.2F;
  float pixel_scale = static_cast<float>(ref_d / std::max(1.001F, camera_distance));
  pixel_scale *= std::sqrt(static_cast<float>(std::max(360, vp_h)) / 720.F);
  pixel_scale = std::clamp(pixel_scale, 0.38F, 2.2F);

  /// 每条大圆/纬线圆上的分段数须随 `step` 增大，否则放大后折线偏离球面圆弧过明显。
  constexpr int kGlobeGridMaxSeg = 4096;
  const int mer_seg =
      std::clamp(static_cast<int>(std::lround(360.0 / step)) * 2, 48, kGlobeGridMaxSeg);
  const int par_seg = mer_seg;
  const int mer_half_seg =
      std::clamp(static_cast<int>(std::lround(180.0 / step)) * 2, 32, kGlobeGridMaxSeg);

  const int n_mer = static_cast<int>(std::lround(360.0 / step));
  const int k0 = static_cast<int>(std::floor(-90.0 / step + 1e-9));
  const int k1 = static_cast<int>(std::ceil(90.0 / step - 1e-9));

  /// 线网条数封顶，且**须与 HUD 地面视场一致**：`visible_sphere_diameter_deg` 在贴球时接近 180°，
  /// 不能反映屏幕上实际只有零点几度的情况；放大后按 `globe_patch_diameter_deg` 只绘制视口附近的经/纬线带。
  const int n_par_idx = k1 - k0 + 1;
  const double patch_deg = globe_patch_diameter_deg(vp_w, vp_h, camera_distance);
  int i_draw_lo = 0;
  int i_draw_hi = std::max(0, n_mer - 1);
  int k_draw_lo = k0;
  int k_draw_hi = k1;
  const bool use_local_band =
      viewport_center_valid && patch_deg < 92.0 && n_mer > 1;

  int mer_stride = 1;
  int par_stride = 1;
  if (use_local_band) {
    const double dnm_pre = static_cast<double>(n_mer);
    int i_c = static_cast<int>(std::lround((viewport_center_lon_deg + 180.0) * dnm_pre / 360.0));
    i_c = std::clamp(i_c, 0, std::max(0, n_mer - 1));
    const double deg_mer = 360.0 / dnm_pre;
    const int half_i = std::max(3, static_cast<int>(std::ceil((patch_deg * 0.92) / deg_mer)));
    i_draw_lo = std::max(0, i_c - half_i);
    i_draw_hi = std::min(n_mer - 1, i_c + half_i);

    int k_c = static_cast<int>(std::lround(viewport_center_lat_deg / step));
    k_c = std::clamp(k_c, k0, k1);
    const int half_k = std::max(2, static_cast<int>(std::ceil((patch_deg * 0.92) / step)));
    k_draw_lo = std::clamp(k_c - half_k, k0, k1);
    k_draw_hi = std::clamp(k_c + half_k, k0, k1);

    const int band_mer = i_draw_hi - i_draw_lo + 1;
    constexpr int kMerDrawBudget = 160;
    const int mer_stride_budget = std::max(1, (band_mer + kMerDrawBudget - 1) / kMerDrawBudget);
    const int mer_stride_density =
        std::max(1, static_cast<int>(std::floor((patch_deg / step) / 9.0)));
    mer_stride = std::min(mer_stride_budget, mer_stride_density);

    const int band_par = k_draw_hi - k_draw_lo + 1;
    constexpr int kParDrawBudget = 120;
    const int par_stride_budget = std::max(1, (band_par + kParDrawBudget - 1) / kParDrawBudget);
    const int par_stride_density =
        std::max(1, static_cast<int>(std::floor((patch_deg / step) / 9.0)));
    par_stride = std::min(par_stride_budget, par_stride_density);
  } else {
    constexpr int kGlobeMaxMeridiansDraw = 168;
    constexpr int kGlobeMaxParallelsDraw = 84;
    mer_stride = std::max(1, (n_mer + kGlobeMaxMeridiansDraw - 1) / kGlobeMaxMeridiansDraw);
    par_stride = std::max(1, (n_par_idx + kGlobeMaxParallelsDraw - 1) / kGlobeMaxParallelsDraw);
  }

  int n_par_draw = 0;
  for (int k = k0; k <= k1; ++k) {
    const double lat_x = static_cast<double>(k) * step;
    if (lat_x <= -90.0 + 1e-6 || lat_x >= 90.0 - 1e-6) {
      continue;
    }
    n_par_draw++;
  }
  /// 注记步长：`span` 为视场角直径（相机**越近** `span` **越大**，越远越小）。步长须与 `span` **反向**：
  /// 若用 `lbl ∝ span`，会出现拉远时步长过小（满屏字）、放大时步长过大（几乎无字）。
  /// 用 `(360-span)`、`(180-span)` 使放大时步长减小、拉远时增大。
  constexpr double kTargetMeridianLabels = 9.0;
  constexpr double kTargetParallelLabels = 8.0;
  constexpr double kPolarLatCapDeg = 76.0;
  constexpr double kLabelMinCos = 0.22;
  const double span_clamped = std::clamp(span, 0.0, 359.0);
  const double mer_w = std::max(1.0, 360.0 - span_clamped);
  const double par_w = std::max(1.0, 180.0 - std::min(span_clamped, 180.0));
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
  lbl_mer = std::max(lbl_mer, mer_stride);
  lbl_par = std::max(lbl_par, par_stride);

  /// 使最接近视口中心的经线/纬线索引落在采样格上；否则从 i=0、k=k0 起步时中心经度常落在两格之间，放大后视窗内无注记。
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
  mer_label_i0 -= mer_label_i0 % mer_stride;
  {
    const int pk_rem = (par_label_k0 - k0) % par_stride;
    par_label_k0 -= pk_rem;
  }

  constexpr float kCrosshairZoomMaxDist = 4.6F;
  const bool use_crosshair_labels =
      viewport_center_valid && camera_distance < kCrosshairZoomMaxDist && n_mer > 0;
  int i_center_m = -1;
  int k_center_m = k0 - 999999;
  if (use_crosshair_labels) {
    const double dnm_c = static_cast<double>(n_mer);
    i_center_m = static_cast<int>(std::lround((viewport_center_lon_deg + 180.0) * dnm_c / 360.0));
    i_center_m = std::clamp(i_center_m, 0, n_mer - 1);
    k_center_m = static_cast<int>(std::lround(viewport_center_lat_deg / step));
    k_center_m = std::clamp(k_center_m, k0, k1);
  }

  const float lat_off_px = 6.8F * pixel_scale;
  const float lon_off_px = 6.8F * pixel_scale;

  glDisable(GL_TEXTURE_2D);
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  /// 略将线推向近裁剪面，减轻与球面及线–线交点处的深度抖动（半透明线时尤其明显）。
  glEnable(GL_POLYGON_OFFSET_LINE);
  glPolygonOffset(0.F, -3.F);
  glLineWidth(1.0F);
  glColor4f(0.78F, 0.82F, 0.86F, 0.52F);

  std::vector<double> par_lats_for_meridian_merge;
  par_lats_for_meridian_merge.reserve(
      static_cast<size_t>(std::max(8, (k_draw_hi - k_draw_lo) / std::max(1, par_stride) + 6)));
  const auto dedupe_sorted_coords = [](std::vector<double>& v) {
    if (v.empty()) {
      return;
    }
    size_t w = 0;
    for (size_t r = 1; r < v.size(); ++r) {
      if (std::fabs(v[r] - v[w]) > 1e-8) {
        v[++w] = v[r];
      }
    }
    v.resize(w + 1);
  };

  if (use_local_band) {
    for (int kk = k_draw_lo; kk <= k_draw_hi; kk += par_stride) {
      const double la = static_cast<double>(kk) * step;
      if (la > -90.0 + 1e-12 && la < 90.0 - 1e-12) {
        par_lats_for_meridian_merge.push_back(la);
      }
    }
    if (k_center_m >= k_draw_lo && k_center_m <= k_draw_hi && k_center_m >= k0 && k_center_m <= k1 &&
        ((k_center_m - k_draw_lo) % par_stride) != 0) {
      const double la = static_cast<double>(k_center_m) * step;
      if (la > -90.0 + 1e-12 && la < 90.0 - 1e-12) {
        par_lats_for_meridian_merge.push_back(la);
      }
    }
  } else {
    for (int kk = k0; kk <= k1; ++kk) {
      const double la = static_cast<double>(kk) * step;
      if (la > -90.0 + 1e-12 && la < 90.0 - 1e-12) {
        par_lats_for_meridian_merge.push_back(la);
      }
    }
  }
  std::sort(par_lats_for_meridian_merge.begin(), par_lats_for_meridian_merge.end());
  dedupe_sorted_coords(par_lats_for_meridian_merge);

  std::vector<double> mer_lons_draw;
  mer_lons_draw.reserve(static_cast<size_t>(std::max(32, i_draw_hi - i_draw_lo + 4)));
  {
    const double dnm = static_cast<double>(n_mer);
    if (use_local_band) {
      for (int ii = i_draw_lo; ii <= i_draw_hi; ii += mer_stride) {
        mer_lons_draw.push_back(-180.0 + static_cast<double>(ii) * 360.0 / dnm);
      }
      if (i_center_m >= 0 && (i_center_m % mer_stride) != 0 && i_center_m >= i_draw_lo &&
          i_center_m <= i_draw_hi) {
        mer_lons_draw.push_back(-180.0 + static_cast<double>(i_center_m) * 360.0 / dnm);
      }
    } else {
      for (int ii = 0; ii < n_mer; ii += mer_stride) {
        mer_lons_draw.push_back(-180.0 + static_cast<double>(ii) * 360.0 / dnm);
      }
      if (i_center_m >= 0 && (i_center_m % mer_stride) != 0) {
        mer_lons_draw.push_back(-180.0 + static_cast<double>(i_center_m) * 360.0 / dnm);
      }
    }
    std::sort(mer_lons_draw.begin(), mer_lons_draw.end());
    dedupe_sorted_coords(mer_lons_draw);
  }

  auto draw_meridian_i = [&](int i) {
    const double lon_deg = -180.0 + static_cast<double>(i) * (360.0 / static_cast<double>(n_mer));
    const bool emphasis =
        std::fabs(lon_deg) < 1e-6 || std::fabs(std::fabs(lon_deg) - 180.0) < 1e-6;
    if (emphasis) {
      glLineWidth(1.2F);
      glColor4f(0.88F, 0.9F, 0.93F, 0.62F);
    }
    /// 均匀纬度序列与「当前绘制的纬线」纬度双路归并，避免每帧对每条经线做全量 sort。
    glBegin(GL_LINE_STRIP);
    int iu = 0;
    size_t ig = 0;
    const int nu = mer_half_seg + 1;
    const size_t ng = par_lats_for_meridian_merge.size();
    while (iu < nu || ig < ng) {
      const double u = (iu < nu)
          ? (-90.0 + 180.0 * static_cast<double>(iu) / static_cast<double>(mer_half_seg))
          : 9e300;
      const double g = (ig < ng) ? par_lats_for_meridian_merge[ig] : 9e300;
      double lat_deg;
      if (std::fabs(u - g) <= 1e-8) {
        lat_deg = u;
        ++iu;
        ++ig;
      } else if (u < g) {
        lat_deg = u;
        ++iu;
      } else {
        lat_deg = g;
        ++ig;
      }
      double x = 0.;
      double y = 0.;
      double z = 0.;
      lonlat_deg_to_unit_sphere(lon_deg, lat_deg, x, y, z);
      glVertex3d(x * radius, y * radius, z * radius);
    }
    glEnd();
    if (emphasis) {
      glLineWidth(1.0F);
      glColor4f(0.78F, 0.82F, 0.86F, 0.52F);
    }
  };

  if (use_local_band) {
    for (int i = i_draw_lo; i <= i_draw_hi; i += mer_stride) {
      draw_meridian_i(i);
    }
    if (i_center_m >= 0 && (i_center_m % mer_stride) != 0 && i_center_m >= i_draw_lo &&
        i_center_m <= i_draw_hi) {
      draw_meridian_i(i_center_m);
    }
  } else {
    for (int i = 0; i < n_mer; i += mer_stride) {
      draw_meridian_i(i);
    }
    if (i_center_m >= 0 && (i_center_m % mer_stride) != 0) {
      draw_meridian_i(i_center_m);
    }
  }

  auto draw_parallel_k = [&](int k) {
    const double lat_deg = static_cast<double>(k) * step;
    if (lat_deg <= -90.0 + 1e-6 || lat_deg >= 90.0 - 1e-6) {
      return;
    }
    const bool emphasis = std::fabs(lat_deg) < 1e-6;
    if (emphasis) {
      glLineWidth(1.2F);
      glColor4f(0.88F, 0.9F, 0.93F, 0.62F);
    }
    glBegin(GL_LINE_STRIP);
    int ju = 0;
    size_t im = 0;
    const int nu = par_seg + 1;
    const size_t nm = mer_lons_draw.size();
    while (ju < nu || im < nm) {
      const double u = (ju < nu)
          ? (-180.0 + 360.0 * static_cast<double>(ju) / static_cast<double>(par_seg))
          : 9e300;
      const double m = (im < nm) ? mer_lons_draw[im] : 9e300;
      double lon_deg;
      if (std::fabs(u - m) <= 1e-8) {
        lon_deg = u;
        ++ju;
        ++im;
      } else if (u < m) {
        lon_deg = u;
        ++ju;
      } else {
        lon_deg = m;
        ++im;
      }
      double x = 0.;
      double y = 0.;
      double z = 0.;
      lonlat_deg_to_unit_sphere(lon_deg, lat_deg, x, y, z);
      glVertex3d(x * radius, y * radius, z * radius);
    }
    glEnd();
    if (emphasis) {
      glLineWidth(1.0F);
      glColor4f(0.78F, 0.82F, 0.86F, 0.52F);
    }
  };

  if (use_local_band) {
    for (int k = k_draw_lo; k <= k_draw_hi; k += par_stride) {
      draw_parallel_k(k);
    }
    if (k_center_m >= k_draw_lo && k_center_m <= k_draw_hi && ((k_center_m - k_draw_lo) % par_stride) != 0) {
      draw_parallel_k(k_center_m);
    }
  } else {
    for (int k = k0; k <= k1; k += par_stride) {
      draw_parallel_k(k);
    }
    if (k_center_m >= k0 && k_center_m <= k1 && ((k_center_m - k0) % par_stride) != 0) {
      draw_parallel_k(k_center_m);
    }
  }

  if (labels_out != nullptr) {
    const float lat_ox = 0.F;
    const float lat_oy = lat_off_px;
    const float lon_ox = lon_off_px;
    const float lon_oy = 0.F;

    auto emit_label_if = [&](double px, double py, double pz, float ox, float oy, const char* text) {
      if (text == nullptr || text[0] == '\0') {
        return;
      }
      if (!grid_ns::surface_point_faces_camera(content_R, px, py, pz, eye_x, eye_y, eye_z)) {
        return;
      }
      if (!grid_ns::surface_point_label_view_ok(content_R, px, py, pz, eye_x, eye_y, eye_z,
                                                kLabelMinCos)) {
        return;
      }
      append_lonlat_grid_label(labels_out, vp_w, vp_h, pixel_scale, px, py, pz, ox, oy, text);
    };

    /// 放大后：过视口中心的经线只标纬度、纬线只标经度，中心交点合并显示（参考 AFSIM 式十字注记）。
    if (use_crosshair_labels) {
      const double dnm = static_cast<double>(n_mer);
      const int i_center = i_center_m;
      const int k_center = k_center_m;

      const double lon_c = -180.0 + static_cast<double>(i_center) * (360.0 / dnm);
      const double lat_c = static_cast<double>(k_center) * step;
      if (lat_c > -90.0 + 1e-6 && lat_c < 90.0 - 1e-6 && std::fabs(lat_c) <= kPolarLatCapDeg) {
        double xc = 0.;
        double yc = 0.;
        double zc = 0.;
        lonlat_deg_to_unit_sphere(lon_c, lat_c, xc, yc, zc);
        const double pxc = xc * radius;
        const double pyc = yc * radius;
        const double pzc = zc * radius;
        char lonbuf_c[20]{};
        char latbuf_c[20]{};
        char comb[56]{};
        fmt_lon(lonbuf_c, sizeof(lonbuf_c), lon_c, step);
        fmt_lat(latbuf_c, sizeof(latbuf_c), lat_c, step);
        std::snprintf(comb, sizeof(comb), "%s %s", latbuf_c, lonbuf_c);
        emit_label_if(pxc, pyc, pzc, 0.F, 0.F, comb);
      }

      /// 同纬度（过中心的纬线）：只标经度；步长与绘线 `mer_stride` 一致，避免「有字无线」。
      for (int i = 0; i < n_mer; i += mer_stride) {
        if (i == i_center) {
          continue;
        }
        const double lon_deg = -180.0 + static_cast<double>(i) * (360.0 / dnm);
        const double lat_deg = static_cast<double>(k_center) * step;
        if (lat_deg <= -90.0 + 1e-6 || lat_deg >= 90.0 - 1e-6) {
          continue;
        }
        double x = 0.;
        double y = 0.;
        double z = 0.;
        lonlat_deg_to_unit_sphere(lon_deg, lat_deg, x, y, z);
        const double px = x * radius;
        const double py = y * radius;
        const double pz = z * radius;
        char lonbuf[20]{};
        fmt_lon(lonbuf, sizeof(lonbuf), lon_deg, step);
        emit_label_if(px, py, pz, lon_ox, lon_oy, lonbuf);
      }

      /// 同经度（过中心的经线）：只标纬度；步长与绘线 `par_stride` 一致。
      for (int k = k0; k <= k1; k += par_stride) {
        if (k == k_center) {
          continue;
        }
        const double lat_deg = static_cast<double>(k) * step;
        if (lat_deg <= -90.0 + 1e-6 || lat_deg >= 90.0 - 1e-6) {
          continue;
        }
        if (std::fabs(lat_deg) > kPolarLatCapDeg) {
          continue;
        }
        const double lon_deg = -180.0 + static_cast<double>(i_center) * (360.0 / dnm);
        double x = 0.;
        double y = 0.;
        double z = 0.;
        lonlat_deg_to_unit_sphere(lon_deg, lat_deg, x, y, z);
        const double px = x * radius;
        const double py = y * radius;
        const double pz = z * radius;
        char latbuf[20]{};
        fmt_lat(latbuf, sizeof(latbuf), lat_deg, step);
        emit_label_if(px, py, pz, lat_ox, lat_oy, latbuf);
      }
    } else {
      for (int i = mer_label_i0; i < n_mer; i += lbl_mer) {
        const double lon_deg = -180.0 + static_cast<double>(i) * (360.0 / static_cast<double>(n_mer));
        for (int k = par_label_k0; k <= k1; k += lbl_par) {
          const double lat_deg = static_cast<double>(k) * step;
          if (lat_deg <= -90.0 + 1e-6 || lat_deg >= 90.0 - 1e-6) {
            continue;
          }
          if (std::fabs(lat_deg) > kPolarLatCapDeg) {
            continue;
          }
          double x = 0.;
          double y = 0.;
          double z = 0.;
          lonlat_deg_to_unit_sphere(lon_deg, lat_deg, x, y, z);
          const double px = x * radius;
          const double py = y * radius;
          const double pz = z * radius;
          if (!grid_ns::surface_point_faces_camera(content_R, px, py, pz, eye_x, eye_y, eye_z)) {
            continue;
          }
          if (!grid_ns::surface_point_label_view_ok(content_R, px, py, pz, eye_x, eye_y, eye_z,
                                                    kLabelMinCos)) {
            continue;
          }

          char lonbuf[20]{};
          char latbuf[20]{};
          fmt_lon(lonbuf, sizeof(lonbuf), lon_deg, step);
          fmt_lat(latbuf, sizeof(latbuf), lat_deg, step);

          append_lonlat_grid_label(labels_out, vp_w, vp_h, pixel_scale, px, py, pz, lat_ox, lat_oy,
                                   latbuf);
          append_lonlat_grid_label(labels_out, vp_w, vp_h, pixel_scale, px, py, pz, lon_ox, lon_oy,
                                   lonbuf);
        }
      }
    }
  }

  glDisable(GL_POLYGON_OFFSET_LINE);
  glPolygonOffset(0.F, 0.F);
  glDisable(GL_BLEND);
  glLineWidth(1.F);
  glColor4f(1.F, 1.F, 1.F, 1.F);
}

void GlobeEarthView::reset_content_orientation() noexcept {
  static const double kI[16] = {1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.};
  std::memcpy(content_R_, kI, sizeof(kI));
  arcball_pending_ = false;
}

void GlobeEarthView::queue_arcball_drag(int prev_mx, int prev_my, int curr_mx, int curr_my) noexcept {
  arc_prev_mx_ = prev_mx;
  arc_prev_my_ = prev_my;
  arc_curr_mx_ = curr_mx;
  arc_curr_my_ = curr_my;
  arcball_pending_ = true;
}

void GlobeEarthView::clear_arcball_pending() noexcept { arcball_pending_ = false; }

void GlobeEarthView::compute_eye(float yaw, float pitch, float dist, float& ex, float& ey, float& ez) {
  const float cp = std::cos(pitch);
  const float sp = std::sin(pitch);
  const float cy = std::cos(yaw);
  const float sy = std::sin(yaw);
  ex = dist * cp * sy;
  ey = dist * sp;
  ez = dist * cp * cy;
}

void GlobeEarthView::load_modelview_lookat_mult_r() {
  float ex = 0.F;
  float ey = 0.F;
  float ez = 0.F;
  compute_eye(cam_.yaw, cam_.pitch, cam_.distance, ex, ey, ez);
  gluLookAt(static_cast<double>(ex), static_cast<double>(ey), static_cast<double>(ez), 0.0, 0.0, 0.0, 0.0, 1.0,
            0.0);
  glMultMatrixd(content_R_);
}

void GlobeEarthView::setup_projection_and_modelview(int vp_w, int vp_h) {
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  const double aspect =
      static_cast<double>(std::max(1, vp_w)) / static_cast<double>(std::max(1, vp_h));
  {
    const float dist = cam_.distance;
    const double h_eye = std::max(0.0, static_cast<double>(dist) - 1.0);
    double znear = h_eye * 0.05;
    if (znear < 1e-6) {
      znear = 1e-6;
    }
    if (znear > 0.08) {
      znear = 0.08;
    }
    gluPerspective(50.0, aspect, znear, 100.0);
  }
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  load_modelview_lookat_mult_r();
  process_pending_arcball(vp_w, vp_h);
  glLoadIdentity();
  load_modelview_lookat_mult_r();
}

void GlobeEarthView::process_pending_arcball(int vp_w, int vp_h) {
  if (!arcball_pending_) {
    return;
  }
  double u0[3]{};
  double u1[3]{};
  if (try_pixel_unit_world(arc_prev_mx_, arc_prev_my_, vp_w, vp_h, u0[0], u0[1], u0[2]) &&
      try_pixel_unit_world(arc_curr_mx_, arc_curr_my_, vp_w, vp_h, u1[0], u1[1], u1[2])) {
    float ex = 0.F;
    float ey = 0.F;
    float ez = 0.F;
    compute_eye(cam_.yaw, cam_.pitch, cam_.distance, ex, ey, ez);
    const double dex = static_cast<double>(ex);
    const double dey = static_cast<double>(ey);
    const double dez = static_cast<double>(ez);
    double Rd[16]{};
    double Rtmp[16]{};
    const double elen = std::sqrt(dex * dex + dey * dey + dez * dez);
    /// `u0,u1` 在**地球模型系**；`Rd` 将 `u0` 旋到 `u1`（物体系增量）。须 **右乘**：`R_new = R_old * Rd`，与 `glMultMatrix` 后乘一致；`Rd * R_old` 会把弧球当成世界系旋转，竖拖会与鼠标反向或乱轴。
    cw::math::Quat q_s{};
    cw::math::quat_from_two_unit_vectors(u0, u1, q_s);
    cw::math::quat_to_mat4_col(q_s, Rd);
    cw::math::mat4_mul_col_major(content_R_, Rd, Rtmp);
    if (elen > 1e-12) {
      north_roll_align_content_R(Rtmp, static_cast<double>(ex), static_cast<double>(ey),
                                 static_cast<double>(ez));
    }
    for (int i = 0; i < 16; ++i) {
      content_R_[i] = Rtmp[i];
    }
    arcball_pending_ = false;
  }
}

bool GlobeEarthView::try_pixel_unit_world(int mx, int my, int vp_w, int vp_h, double& ux, double& uy,
                                          double& uz) const {
  if (vp_w < 1 || vp_h < 1 || mx < 0 || my < 0 || mx >= vp_w || my >= vp_h) {
    return false;
  }
  GLint view[4]{};
  GLdouble model[16]{};
  GLdouble proj[16]{};
  glGetIntegerv(GL_VIEWPORT, view);
  glGetDoublev(GL_MODELVIEW_MATRIX, model);
  glGetDoublev(GL_PROJECTION_MATRIX, proj);
  const GLdouble winx = static_cast<GLdouble>(mx);
  const GLdouble winy = static_cast<GLdouble>(vp_h - 1 - my);
  GLdouble ax = 0.;
  GLdouble ay = 0.;
  GLdouble az = 0.;
  GLdouble bx = 0.;
  GLdouble by = 0.;
  GLdouble bz = 0.;
  if (gluUnProject(winx, winy, 0.0, model, proj, view, &ax, &ay, &az) != GL_TRUE) {
    return false;
  }
  if (gluUnProject(winx, winy, 1.0, model, proj, view, &bx, &by, &bz) != GL_TRUE) {
    return false;
  }
  const double vx = bx - ax;
  const double vy = by - ay;
  const double vz = bz - az;
  const double a = vx * vx + vy * vy + vz * vz;
  if (a < 1e-24) {
    return false;
  }
  const double b = 2.0 * (ax * vx + ay * vy + az * vz);
  const double c = ax * ax + ay * ay + az * az - 1.0;
  const double disc = b * b - 4.0 * a * c;
  if (disc < 0.0) {
    return false;
  }
  const double sdisc = std::sqrt(disc);
  const double t0 = (-b - sdisc) / (2.0 * a);
  const double t1 = (-b + sdisc) / (2.0 * a);
  float ex = 0.F;
  float ey = 0.F;
  float ez = 0.F;
  compute_eye(cam_.yaw, cam_.pitch, cam_.distance, ex, ey, ez);
  const double cex = static_cast<double>(ex);
  const double cey = static_cast<double>(ey);
  const double cez = static_cast<double>(ez);

  /// 射线与球面可能有两个正根；须取**朝向相机一侧**的交点（世界系下 `dot(p_world, eye) > 0`）。
  /// `gluUnProject` 给出的是**地球模型系**点；`p_world = content_R * p_body`（勿用完整 MODELVIEW，否则与 `eye` 世界坐标混用）。
  auto world_dot_eye = [&](double tc) -> double {
    const double px = ax + tc * vx;
    const double py = ay + tc * vy;
    const double pz = az + tc * vz;
    const double wx = content_R_[0] * px + content_R_[4] * py + content_R_[8] * pz;
    const double wy = content_R_[1] * px + content_R_[5] * py + content_R_[9] * pz;
    const double wz = content_R_[2] * px + content_R_[6] * py + content_R_[10] * pz;
    return wx * cex + wy * cey + wz * cez;
  };

  double t = -1.0;
  const double candidates[2] = {t0, t1};
  double best_t_front = -1.0;
  double best_t_any = -1.0;
  for (double tc : candidates) {
    if (tc <= 0.0) {
      continue;
    }
    const double d = world_dot_eye(tc);
    if (best_t_any < 0.0 || tc < best_t_any) {
      best_t_any = tc;
    }
    if (d > 1e-9 && (best_t_front < 0.0 || tc < best_t_front)) {
      best_t_front = tc;
    }
  }
  if (best_t_front >= 0.0) {
    t = best_t_front;
  } else if (best_t_any >= 0.0) {
    t = best_t_any;
  }
  if (t < 0.0) {
    return false;
  }
  const double px = ax + t * vx;
  const double py = ay + t * vy;
  const double pz = az + t * vz;
  const double len = std::sqrt(px * px + py * py + pz * pz);
  if (len < 1e-15) {
    return false;
  }
  ux = px / len;
  uy = py / len;
  uz = pz / len;
  return true;
}

bool GlobeEarthView::try_pixel_lonlat(int mx, int my, int vp_w, int vp_h, double& lon_deg,
                                      double& lat_deg) const {
  double nx = 0.;
  double ny = 0.;
  double nz = 0.;
  if (!try_pixel_unit_world(mx, my, vp_w, vp_h, nx, ny, nz)) {
    return false;
  }
  constexpr double kPi = 3.14159265358979323846;
  lat_deg = std::asin(std::max(-1.0, std::min(1.0, ny))) * (180.0 / kPi);
  lon_deg = std::atan2(nx, nz) * (180.0 / kPi);
  return true;
}

void GlobeEarthView::mat4_mul_col(const double* a, const double* b, double* o) {
  cw::math::mat4_mul_col_major(a, b, o);
}

void GlobeEarthView::rot_align_u_to_v(const double u[3], const double v[3], double out_R[16]) {
  cw::math::rot_align_unit_vectors_to_mat4_col(u, v, out_R);
}

}  // namespace cw::render
