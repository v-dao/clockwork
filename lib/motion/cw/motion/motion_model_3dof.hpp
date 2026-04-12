#pragma once

#include "cw/motion/motion_model.hpp"

namespace cw::motion {

/// 三自由度平动模型：位置/速度在世界系（墨卡托局地 ENU）下积分；可选沿 `route` 航路点飞行。
/// 无航线时保持当前速度（来自想定初值）。
class MotionModel3dof final : public MotionModel {
 public:
  void apply_dynamics(MoverRuntimeState& state, const MoverStepInput& in) const override;
};

}  // namespace cw::motion
