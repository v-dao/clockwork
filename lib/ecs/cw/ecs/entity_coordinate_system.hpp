#pragma once

#include "cw/vec3.hpp"

namespace cw::ecs {

/// 机体坐标系（ECS）与墨卡托局地世界系（东-北-天）的变换。
///
/// **机体轴**（与 **NED** 一致）：**X** 北向机头、**Y** 东向右翼、**Z** 向下。
/// **局地世界轴**（与 `entity_pos` 墨卡托米一致）：**x** 东、**y** 北、**z** 天。
///
/// **`entity_att` 角度**在 **NED** 下定义，**欧拉旋转顺序为 Z → Y → X**（先绕下地轴偏航，再绕东向轴俯仰，再绕北向轴滚转），
/// 即固定 NED 轴上：`R_ned = R_X(roll) * R_Y(pitch) * R_Z(yaw)`，再乘以 **NED→ENU** 将速度/机头转到墨卡托局地系。
///
/// 角含义（度）：**偏航** 0° 北、90° 东（从北顺时针）；**俯仰** 0° 水平、+90° 朝天、−90° 朝地；
/// **滚转** 沿机头 **+X** 从机尾向机头看顺时针为正（右手绕北向轴）。
class EntityCoordinateSystem {
 public:
  [[nodiscard]] static cw::math::Vec3 body_velocity_to_world_mercator(const cw::math::Vec3& v_body_mps,
                                                                     float yaw_deg, float pitch_deg,
                                                                     float roll_deg) noexcept;

  [[nodiscard]] static cw::math::Vec3 body_forward_world_mercator(float yaw_deg, float pitch_deg,
                                                                  float roll_deg) noexcept;

  [[nodiscard]] static float icon_rotation_deg_mercator(float yaw_deg, float pitch_deg,
                                                        float roll_deg) noexcept;
};

}  // namespace cw::ecs
