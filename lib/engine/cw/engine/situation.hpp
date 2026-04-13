#pragma once

#include "cw/engine/types.hpp"
#include "cw/scenario/scenario.hpp"
#include "cw/vec3.hpp"

#include <string>
#include <vector>

namespace cw::engine {

/// 单次传感器探测记录（阶段 3 MVP）。
struct SensorDetection {
  EntityId observer_id = 0;
  EntityId target_id = 0;
  float range_m = 0.F;
  float reported_rcs_m2 = 0.F;
};

/// Per-entity data exposed to 态势显示 / 记录器。
struct EntitySituation {
  EntityId id = 0;
  std::string external_id;
  std::string name;
  std::string faction;
  std::string variant_ref;
  std::string icon_2d_path;
  std::string model_3d_path;
  bool has_display_color = false;
  float display_color_r = 1.F;
  float display_color_g = 1.F;
  float display_color_b = 1.F;
  cw::math::Vec3 position{};
  cw::math::Vec3 velocity{};
  cw::math::Vec3 angular_velocity{};
  /// 与想定 `entity_att` 一致（度）；供显示与传感器轴向。
  float yaw_deg = 0.F;
  float pitch_deg = 0.F;
  float roll_deg = 0.F;
};

/// 单帧态势快照。
struct SituationSnapshot {
  double sim_time = 0.0;
  double time_scale = 1.0;
  EngineState engine_state = EngineState::Uninitialized;
  std::vector<EntitySituation> entities;
  std::vector<SensorDetection> sensor_detections;
};

/// 显示/录制等**只读**消费边界：当前帧态势 + 想定静态图层（不含仿真控制 API）。
/// 由 `Engine::situation_presentation()` 绑定引擎内部存储；调用方勿长期持有跨 `step()` 的引用（见 A10 / architecture）。
struct SituationPresentation {
  const SituationSnapshot& situation;
  const std::vector<cw::scenario::ScenarioRoute>& routes;
  const std::vector<cw::scenario::ScenarioAirspace>& airspaces;
};

}  // namespace cw::engine
