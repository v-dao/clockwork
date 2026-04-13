#include "cw/ecs/entity_coordinate_system.hpp"

#include "cw/math/constants.hpp"

#include <cmath>

namespace cw::ecs {

namespace {

void mat3_mul_col(const double A[9], const double B[9], double C[9]) {
  for (int c = 0; c < 3; ++c) {
    const double b0 = B[c * 3 + 0];
    const double b1 = B[c * 3 + 1];
    const double b2 = B[c * 3 + 2];
    for (int r = 0; r < 3; ++r) {
      C[c * 3 + r] = A[0 * 3 + r] * b0 + A[1 * 3 + r] * b1 + A[2 * 3 + r] * b2;
    }
  }
}

void mat3_mul_vec3(const double M[9], const double v[3], double out[3]) {
  for (int r = 0; r < 3; ++r) {
    out[r] = M[0 * 3 + r] * v[0] + M[1 * 3 + r] * v[1] + M[2 * 3 + r] * v[2];
  }
}

/// NED：绕 **X（北）** 轴，右手系正角（滚转）。
void rot_x_ned(double angle_rad, double out[9]) {
  const double c = std::cos(angle_rad);
  const double s = std::sin(angle_rad);
  out[0] = 1.;
  out[1] = 0.;
  out[2] = 0.;
  out[3] = 0.;
  out[4] = c;
  out[5] = s;
  out[6] = 0.;
  out[7] = -s;
  out[8] = c;
}

/// NED：绕 **Y（东）** 轴，右手系正角（俯仰）。
void rot_y_ned(double angle_rad, double out[9]) {
  const double c = std::cos(angle_rad);
  const double s = std::sin(angle_rad);
  out[0] = c;
  out[1] = 0.;
  out[2] = -s;
  out[3] = 0.;
  out[4] = 1.;
  out[5] = 0.;
  out[6] = s;
  out[7] = 0.;
  out[8] = c;
}

/// NED：绕 **Z（下）** 轴，右手系正角（偏航）。0° 朝北、90° 朝东（从北顺时针与 RH 绕 +Z_down 一致）。
void rot_z_ned(double yaw_rad, double out[9]) {
  const double c = std::cos(yaw_rad);
  const double s = std::sin(yaw_rad);
  out[0] = c;
  out[1] = s;
  out[2] = 0.;
  out[3] = -s;
  out[4] = c;
  out[5] = 0.;
  out[6] = 0.;
  out[7] = 0.;
  out[8] = 1.;
}

/// 机体与 NED 轴对齐时 **NED → 局地 ENU（墨卡托）**：(E,N,U) = (Y_ned, X_ned, -Z_ned)。
void ned_to_enu(double out[9]) {
  out[0] = 0.;
  out[1] = 1.;
  out[2] = 0.;
  out[3] = 1.;
  out[4] = 0.;
  out[5] = 0.;
  out[6] = 0.;
  out[7] = 0.;
  out[8] = -1.;
}

/// `entity_att`：NED 下固定轴欧拉顺序 **Z → Y → X**，即 `v_ned' = R_x(roll) * R_y(pitch) * R_z(yaw) * v_body`；
/// 再 `v_enu = ned_to_enu * v_ned'`。
void body_to_world_rot(double yaw_deg, double pitch_deg, double roll_deg, double R_out[9]) {
  double T[9]{};
  ned_to_enu(T);
  double Rz[9]{};
  double Ry[9]{};
  double Rx[9]{};
  rot_z_ned(yaw_deg * (cw::math::kPi / 180.0), Rz);
  rot_y_ned(pitch_deg * (cw::math::kPi / 180.0), Ry);
  rot_x_ned(roll_deg * (cw::math::kPi / 180.0), Rx);
  double T1[9]{};
  double T2[9]{};
  mat3_mul_col(Ry, Rz, T1);
  mat3_mul_col(Rx, T1, T2);
  mat3_mul_col(T, T2, R_out);
}

}  // namespace

cw::math::Vec3 EntityCoordinateSystem::body_velocity_to_world_mercator(const cw::math::Vec3& v_body_mps,
                                                                       float yaw_deg, float pitch_deg,
                                                                       float roll_deg) noexcept {
  double R[9]{};
  body_to_world_rot(static_cast<double>(yaw_deg), static_cast<double>(pitch_deg),
                    static_cast<double>(roll_deg), R);
  const double vb[3] = {static_cast<double>(v_body_mps.x), static_cast<double>(v_body_mps.y),
                        static_cast<double>(v_body_mps.z)};
  double vw[3]{};
  mat3_mul_vec3(R, vb, vw);
  return {static_cast<float>(vw[0]), static_cast<float>(vw[1]), static_cast<float>(vw[2])};
}

cw::math::Vec3 EntityCoordinateSystem::body_forward_world_mercator(float yaw_deg, float pitch_deg,
                                                                   float roll_deg) noexcept {
  return body_velocity_to_world_mercator({1.F, 0.F, 0.F}, yaw_deg, pitch_deg, roll_deg);
}

float EntityCoordinateSystem::icon_rotation_deg_mercator(float yaw_deg, float pitch_deg,
                                                         float roll_deg) noexcept {
  const cw::math::Vec3 f = body_forward_world_mercator(yaw_deg, pitch_deg, roll_deg);
  const float h2 = f.x * f.x + f.y * f.y;
  if (h2 < 1e-12F) {
    return yaw_deg + 90.F;
  }
  return std::atan2(f.y, f.x) * 57.2957795F + 90.F;
}

}  // namespace cw::ecs
