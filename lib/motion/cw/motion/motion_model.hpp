#pragma once

#include "cw/vec3.hpp"
#include "cw/scenario/scenario.hpp"

#include <cstddef>
#include <string>

namespace cw::motion {

/// 单实体 mover 一步所需的运行时状态（与引擎 `EntityRecord` 中 mover 相关字段对应）。
struct MoverRuntimeState {
  cw::math::Vec3 position{};
  cw::math::Vec3 velocity{};
  float yaw_deg = 0.F;
  float pitch_deg = 0.F;
  float roll_deg = 0.F;
  std::string route_id;
  std::size_t route_wp_index = 0;
};

/// 一步积分输入：时间步、速度上限、航线数据等。
struct MoverStepInput {
  float dt = 0.F;
  float speed_cap_mps = 100.F;
  float waypoint_arrive_m = 10.F;
  /// 非空时按 `route_id` 在列表中查找航线；空指针表示无航线数据。
  const std::vector<cw::scenario::ScenarioRoute>* routes = nullptr;
  /// `entity_mparam mover track_yaw 1`：沿航线时根据速度矢量更新偏航（默认 true）。
  bool track_yaw_from_velocity = true;
  /// `entity_mparam mover track_pitch 1`：沿航线时根据爬升率更新俯仰（默认 false）。
  bool track_pitch_from_velocity = false;
};

/// 运动模型抽象基类；派生类实现不同自由度与动力学。
class MotionModel {
 public:
  virtual ~MotionModel() = default;

  /// 更新 `state` 的速度（及可选姿态），并由调用方积分位置，或由派生在同一接口内完成积分。
  /// 约定：本方法**会**写入 `velocity`，并在需要时更新 `yaw_deg` / `pitch_deg`；
  /// **位置积分** `position += velocity * dt` 由本方法或引擎统一执行（当前由引擎在 `step` 后积分）。
  virtual void apply_dynamics(MoverRuntimeState& state, const MoverStepInput& in) const = 0;
};

/// 按 `kind` 字符串返回可复用的模型实例（静态生存期）；未知 kind 时回退到三自由度。
[[nodiscard]] const MotionModel& motion_model_for_kind(const std::string& kind);

/// 占位模型：不修改速度与姿态（仍由引擎按当前速度积分位置）。
class MotionModelStub final : public MotionModel {
 public:
  void apply_dynamics(MoverRuntimeState&, const MoverStepInput&) const override {}
};

}  // namespace cw::motion
