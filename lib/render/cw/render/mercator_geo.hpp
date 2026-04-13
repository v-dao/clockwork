#pragma once

#include "cw/math/constants.hpp"

#include <algorithm>
#include <cmath>

namespace cw::render {

/// Web 墨卡托米 x 的周期化（与 situation_view 一致）。
inline double wrap_mercator_lon_x(double x) {
  constexpr double W = 2.0 * 20037508.34;
  double r = std::fmod(x + 0.5 * W, W);
  if (r < 0.0) {
    r += W;
  }
  return r - 0.5 * W;
}

/// EPSG:3857 米 → 经纬度（度）。
inline void mercator_meters_to_lonlat(double x, double y, double& lon_deg, double& lat_deg) {
  constexpr double kR = 6378137.0;
  constexpr double kYMax = 20037508.34;
  const double yc = std::max(-kYMax, std::min(kYMax, y));
  const double lon = wrap_mercator_lon_x(x) / kR;
  const double lat = 2.0 * std::atan(std::exp(yc / kR)) - cw::math::kPi * 0.5;
  lon_deg = lon * (180.0 / cw::math::kPi);
  lat_deg = lat * (180.0 / cw::math::kPi);
}

/// 经纬度（度）→ Web 墨卡托米（`x` 经 `wrap_mercator_lon_x`，`y` 夹到 ±20037508.34）。
inline void lonlat_deg_to_mercator_meters(double lon_deg, double lat_deg, double& mx, double& my) {
  constexpr double kR = 6378137.0;
  constexpr double kYMax = 20037508.34;
  double lon_wrapped = lon_deg;
  while (lon_wrapped <= -180.0) {
    lon_wrapped += 360.0;
  }
  while (lon_wrapped > 180.0) {
    lon_wrapped -= 360.0;
  }
  const double lonr = lon_wrapped * (cw::math::kPi / 180.0);
  const double latr = lat_deg * (cw::math::kPi / 180.0);
  mx = wrap_mercator_lon_x(kR * lonr);
  const double y_raw = kR * std::log(std::tan(cw::math::kPi * 0.25 + latr * 0.5));
  my = std::max(-kYMax, std::min(kYMax, y_raw));
}

/// 经纬度（度）→ 单位球面方向（Y 轴为北，与 situation_view 球坐标一致）。
inline void lonlat_deg_to_unit_sphere(double lon_deg, double lat_deg, double& x, double& y,
                                      double& z) {
  const double lon = lon_deg * (cw::math::kPi / 180.0);
  const double lat = lat_deg * (cw::math::kPi / 180.0);
  const double cl = std::cos(lat);
  x = cl * std::sin(lon);
  y = std::sin(lat);
  z = cl * std::cos(lon);
}

}  // namespace cw::render
