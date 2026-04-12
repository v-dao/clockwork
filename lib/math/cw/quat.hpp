#pragma once

namespace cw::math {

/// 四元数 (w, x, y, z)，Hamilton 积；列向量旋转 `R * v`。
struct Quat {
  double w = 1.;
  double x = 0.;
  double y = 0.;
  double z = 0.;
};

void quat_normalize(Quat& q);

/// 列主序 3×3（与 OpenGL mat4 左上角一致）。
void mat3_col_from_quat(const Quat& q, double m[9]);

void quat_from_axis_angle_unit(const double axis[3], double angle, Quat& q);

/// 将单位向量 `u` 旋到 `v` 的最短旋转（稳定构造，含 `u ≈ -v` 分支）。
void quat_from_two_unit_vectors(const double u[3], const double v[3], Quat& q);

void quat_to_mat4_col(const Quat& q, double out_R[16]);

}  // namespace cw::math
