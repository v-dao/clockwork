#pragma once

#include "cw/engine/engine.hpp"

namespace cw::render {

/// Web 墨卡托战术平面视锥（米，EPSG:3857）。
struct MercatorOrthoFrustum {
  float l = 0.F;
  float r = 0.F;
  float b = 0.F;
  float t = 0.F;
};

/// 实体/航线/空域外包（墨卡托米）。
struct MercatorBounds {
  float min_x = 0.F;
  float max_x = 0.F;
  float min_y = 0.F;
  float max_y = 0.F;
  bool empty = true;

  void add_xy(float x, float y);
};

/// 二维墨卡托地图：平移/缩放视锥与交互（滚轮、左键拖动平移）。
class TacticalMercatorMap {
  struct ViewCamera {
    float pan_mx = 0.F;
    float pan_my = 0.F;
    float zoom = 1.F;
  } cam_{};

public:
  static constexpr float kMercatorHalfExtentM = 20037508.34F;
  static constexpr float kWorldWidthM = 2.0F * kMercatorHalfExtentM;

  void reset_camera() noexcept;
  void apply_wheel_zoom(int wheel_delta) noexcept;
  /// 为真时 `expand_bounds_from_engine` 把仿真实体（及速度提示点）纳入外包盒；默认假（仅航线/空域等静态层）。
  void set_auto_bounds_include_entities(bool enabled) noexcept { auto_bounds_include_entities_ = enabled; }
  [[nodiscard]] bool auto_bounds_include_entities() const noexcept { return auto_bounds_include_entities_; }
  void expand_bounds_from_engine(const cw::engine::Engine& eng, MercatorBounds& b) const;

  void compute_ortho_frustum(const MercatorBounds& b, int vp_w, int vp_h, MercatorOrthoFrustum& f) const;
  void compute_interactive_frustum(const MercatorBounds& b, int vp_w, int vp_h, MercatorOrthoFrustum& tactical);
  void apply_mouse_pan_drag(const cw::engine::Engine& eng, int vp_w, int vp_h, int dx_win, int dy_win);
  /// 将当前视锥中心（墨卡托米）对齐到给定经纬度，保持 `zoom` 不变；用于分屏时与三维地球中心一致。
  void set_frustum_center_lonlat(const cw::engine::Engine& eng, int vp_w, int vp_h, double lon_deg,
                                  double lat_deg);
  /// 调整 `zoom`，使视口中心处东西向地面距离（`(r-l)*cos(纬度)`，Web 墨卡托近似）接近 `physical_ew_m`；`pan` 不变。
  void set_visible_ground_ew_meters_at_lat(const cw::engine::Engine& eng, int vp_w, int vp_h,
                                           double physical_ew_m, double center_lat_deg);

  static void expand_frustum_for_world_basemap(const MercatorOrthoFrustum& tactical, MercatorOrthoFrustum& map);
  static void apply_ortho_frustum(const MercatorOrthoFrustum& f);
  static void mercator_meters_to_tex_uv(double x, double y, float& u, float& v);
  static void window_pixel_to_lonlat(int mx, int my, int vp_w, int vp_h, const MercatorOrthoFrustum& tactical,
                                     double& lon_deg, double& lat_deg);
  static float mercator_periodic_x(float x, float cx_ref) noexcept;

  float zoom() const noexcept { return cam_.zoom; }
  float pan_mx() const noexcept { return cam_.pan_mx; }
  float pan_my() const noexcept { return cam_.pan_my; }

 private:
  bool auto_bounds_include_entities_ = false;
};

}  // namespace cw::render
