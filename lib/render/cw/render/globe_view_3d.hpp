#pragma once

#include "cw/render/lonlat_grid.hpp"

#include <vector>

namespace cw::render {

/// 在当前 MODELVIEW/PROJECTION 已设为 globe 的前提下绘制经纬线；可选收集注记供 HUD 正交阶段绘制。
/// `content_R` 与 `eye_*` 与 `gluLookAt(eye→0)×content_R` 一致，用于剔除背向相机半球上的网格与注记。
/// 注记步长随视场角直径反向变化（近大远小），并剔除高纬与掠射处，避免全节点注记。
/// `camera_distance` 用于自适应经线/纬线间隔；`radius` 略大于 1 可与岸线同层。
/// 若 `viewport_center_valid`，用视口中心经纬对齐注记相位。相机较近时改为十字注记：中心格为「纬 经」合并串，同纬线只标经度、同经线只标纬度。
/// `lonlat_step_deg_override`：>0 时强制使用该经/纬线间隔（度），用于分屏与左侧墨卡托战术图 `draw_tactical_lonlat_grid` 共用同一套网格。
void draw_globe_lonlat_grid(int vp_w, int vp_h, float camera_distance, const double content_R[16],
                            double eye_x, double eye_y, double eye_z, double radius,
                            std::vector<GlobeLonLatLabel>* labels_out, bool viewport_center_valid,
                            double viewport_center_lon_deg, double viewport_center_lat_deg,
                            double lonlat_step_deg_override = 0.0) noexcept;


/// 三维地球：轨道相机 `gluLookAt(eye→0)` + 弧球 `content_R`。
/// 左键拖动：最短弧粘点后，再绕 `normalize(eye)` 滚转，使地理北与 `gluLookAt` 相机上在视平面内对齐（`north_roll_align_content_R`）；粘点在北向滚转后为近似。
class GlobeEarthView {
public:
  struct GlobeCamera {
    float yaw = 0.55F;
    float pitch = 0.35F;
    float distance = 3.2F;
  };

  GlobeCamera& camera() noexcept { return cam_; }
  const GlobeCamera& camera() const noexcept { return cam_; }

  bool lighting_enabled() const noexcept { return lighting_; }
  void set_lighting_enabled(bool v) noexcept { lighting_ = v; }
  void toggle_lighting() noexcept { lighting_ = !lighting_; }

  void reset_content_orientation() noexcept;

  const double* content_R() const noexcept { return content_R_; }
  double* content_R() noexcept { return content_R_; }

  /// 左键拖动：排队弧球增量（上一像素与当前像素射线命中单位球），在 `setup_projection_and_modelview` 中乘入 `content_R`。
  void queue_arcball_drag(int prev_mx, int prev_my, int curr_mx, int curr_my) noexcept;

  /// 取消未应用的弧球队列（例如左键已松开，避免下一帧误用旧像素对）。
  void clear_arcball_pending() noexcept;

  /// 是否有尚未在 `setup_projection_and_modelview` 中乘入 `content_R` 的弧球拖动（分屏同步前可先刷新姿态）。
  bool arcball_pending() const noexcept { return arcball_pending_; }

  /// 设置透视投影并将 MODELVIEW 设为 lookAt(eye→0)×content_R；若本帧有待处理弧球拖动则更新 R 并重载 MODELVIEW。
  void setup_projection_and_modelview(int vp_w, int vp_h);

  bool try_pixel_unit_world(int mx, int my, int vp_w, int vp_h, double& ux, double& uy, double& uz) const;
  bool try_pixel_lonlat(int mx, int my, int vp_w, int vp_h, double& lon_deg, double& lat_deg) const;

  /// 由当前 `yaw/pitch/distance` 与 `content_R` 解析视口中心射线与球面交点的经纬度（与 `try_pixel_lonlat(vp/2)` 一致，不依赖 GL）。
  void viewport_center_lonlat_from_pose(double& lon_deg, double& lat_deg) const noexcept;
  /// 调整 `content_R`，使给定经纬度落在屏幕中心（不改变轨道相机参数）；随后做北向滚转对齐。
  void orient_content_to_place_lonlat_at_screen_center(double lon_deg, double lat_deg) noexcept;

  /// 当前透视与 `distance` 下，视口中心附近东西向地面宽度（米），与 HUD 比例尺算法一致（WGS84 半径、`fovy=50°`）。
  double visible_ground_ew_meters(int vp_w, int vp_h) const noexcept;
  /// 设置 `camera().distance`，使上述东西向地面宽度接近 `physical_ew_m`（与 `setup_projection_and_modelview` 的投影一致）。
  void set_camera_distance_for_visible_ew_meters(int vp_w, int vp_h, double physical_ew_m) noexcept;

  /// 转发 `cw::math::mat4_mul_col_major`（OpenGL 列主序）。
  static void mat4_mul_col(const double* a, const double* b, double* o);
  /// 转发 `cw::math::rot_align_unit_vectors_to_mat4_col`。
  static void rot_align_u_to_v(const double u[3], const double v[3], double out_R[16]);
  static void compute_eye(float yaw, float pitch, float dist, float& ex, float& ey, float& ez);

private:
  void load_modelview_lookat_mult_r();
  void process_pending_arcball(int vp_w, int vp_h);

  GlobeCamera cam_{};
  bool lighting_ = false;
  double content_R_[16] = {1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.};
  bool arcball_pending_ = false;
  int arc_prev_mx_ = 0;
  int arc_prev_my_ = 0;
  int arc_curr_mx_ = 0;
  int arc_curr_my_ = 0;
};

}  // namespace cw::render
