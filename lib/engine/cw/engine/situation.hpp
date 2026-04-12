#pragma once

#include "cw/engine/types.hpp"
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
};

/// 单帧态势快照。
struct SituationSnapshot {
  double sim_time = 0.0;
  double time_scale = 1.0;
  EngineState engine_state = EngineState::Uninitialized;
  std::vector<EntitySituation> entities;
  std::vector<SensorDetection> sensor_detections;
};

}  // namespace cw::engine
