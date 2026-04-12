#pragma once

namespace cw::situation_view {

/// 战术/地球视图底部 HUD 与 `draw_*` 填充的摘要（中心经纬度、比例尺等）。
struct SituationHud {
  double center_lon_deg = 0.;
  double center_lat_deg = 0.;
  double meters_per_px = 0.;
  float zoom_factor = 1.F;
  bool has_cursor_lonlat = false;
  double cursor_lon_deg = 0.;
  double cursor_lat_deg = 0.;
  bool hud_is_globe = false;
  double globe_camera_distance = 3.0;
  double globe_ground_width_m = 0.;
  double globe_scale_approx = 0.;
};

}  // namespace cw::situation_view
