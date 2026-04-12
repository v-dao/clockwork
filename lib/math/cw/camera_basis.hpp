#pragma once

namespace cw::math {

/// 与 `gluLookAt(eye→0, up=(0,1,0))` 一致的相机「上」方向（世界系单位向量）。
void glu_lookat_camera_up_world(double ex, double ey, double ez, double out_u[3]);

/// 与 `gluLookAt` 一致的相机「右」方向（`cross(forward, up)`，世界系单位向量）。
void glu_lookat_camera_side_world(double ex, double ey, double ez, double out_s[3]);

}  // namespace cw::math
