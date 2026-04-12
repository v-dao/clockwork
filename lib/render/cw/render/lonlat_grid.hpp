#pragma once

#include <cstddef>
#include <vector>

namespace cw::render {

struct MercatorOrthoFrustum;

/// 经纬网注记：屏幕像素坐标（与 `glOrtho(0,w,h,0)` 一致，原点在左下）、相对交点的像素偏移、位图缩放、ASCII 文本。
struct GlobeLonLatLabel {
  float sx = 0.F;
  float sy = 0.F;
  float ox = 0.F;
  float oy = 0.F;
  float pixel_scale = 1.F;
  /// 单分量如 `30N`；中心交点可为合并串如 `20N 10E`。
  char text[48]{};
};

/// 轨道相机距离 `d`（球半径=1）下可见球冠的角直径（度），与 `equiv_camera_distance_from_span_deg` 互逆。
double visible_sphere_diameter_deg(float camera_distance) noexcept;

/// 与三维地球相同的步长选取：`visible_diameter_deg` 与 `camera_distance` 须一致（如由同一视场导出）。
double pick_lonlat_step_deg(double visible_diameter_deg, float camera_distance) noexcept;

/// 由视场角直径（度）反推与球面几何一致的等效 `camera_distance`（`h=d-1` 为离地高度，球半径=1）。
float equiv_camera_distance_from_span_deg(double span_deg) noexcept;

/// 墨卡托平面：`span` 大表示拉远（可见经/纬范围大），应对应大三维「远距」相机（大 `d`）。勿用 `equiv_camera_distance_from_span_deg`（球面反演会把大 `span` 当成贴球大视锥→细步长）。
float tactical_equiv_camera_distance_from_span_deg(double span_deg) noexcept;

/// 战术墨卡托视锥在中心经线处的纬向跨度、在 `cx_ref` 经线处的经向跨度，取较大者为 `span_deg`。
double tactical_frustum_lonlat_span_deg(const MercatorOrthoFrustum& tactical, float cx_ref) noexcept;

void fmt_lon(char* buf, size_t cap, double lon_deg, double step_deg) noexcept;
void fmt_lat(char* buf, size_t cap, double lat_deg, double step_deg) noexcept;

bool append_lonlat_grid_label(std::vector<GlobeLonLatLabel>* out, int vp_w, int vp_h, float pixel_scale,
                              double wx, double wy, double wz, float ox, float oy, const char* text) noexcept;

/// 在已 `apply_ortho_frustum(tactical)` 的 GL 状态下绘制墨卡托平面经纬网；可选收集注记供 HUD 阶段绘制。
/// 步长与三维一致；有视口中心时采用十字注记（中心格「纬 经」、过中心纬线只标经度、过中心经线只标纬度；两臂对可见经/纬逐根过滤，与网格绘线一致）。
void draw_tactical_lonlat_grid(int vp_w, int vp_h, const MercatorOrthoFrustum& tactical, float cx_ref,
                               std::vector<GlobeLonLatLabel>* labels_out, bool viewport_center_valid,
                               double viewport_center_lon_deg, double viewport_center_lat_deg) noexcept;

}  // namespace cw::render
