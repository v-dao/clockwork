#include "cw/engine/engine.hpp"
#include "cw/engine/engine_detail.hpp"

#include "cw/ecs/entity_coordinate_system.hpp"
#include "cw/math/constants.hpp"
#include "cw/vec3.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace cw::engine {

namespace {

bool in_sensor_fov_delta(cw::math::Vec3 forward_obs, cw::math::Vec3 delta, float dist, float fov_deg) {
  if (fov_deg >= 359.F) {
    return true;
  }
  cw::math::Vec3 forward = forward_obs;
  if (cw::math::dot(forward, forward) < 1e-6F) {
    forward = {1.F, 0.F, 0.F};
  } else {
    forward = cw::math::normalize(forward);
  }
  if (dist < 1e-3F) {
    return true;
  }
  const cw::math::Vec3 to = cw::math::scale(delta, 1.F / dist);
  const float c = std::clamp(cw::math::dot(forward, to), -1.F, 1.F);
  const float ang_deg = std::acos(c) * 180.F / cw::math::kPiF;
  return ang_deg <= fov_deg * 0.5F;
}

constexpr std::uint64_t pack_cell_xy(std::int32_t ix, std::int32_t iy) noexcept {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(ix)) << 32) |
         static_cast<std::uint32_t>(iy);
}

}  // namespace

void Engine::compute_sensor_detections() {
  using cw::engine::detail::param_float;

  snapshot_.sensor_detections.clear();

  const std::size_t n = entities_.size();
  if (n == 0) {
    return;
  }

  std::vector<std::size_t> sensor_idx;
  sensor_idx.reserve(n / 8U + 1U);
  float max_range_m = 0.F;
  for (std::size_t i = 0; i < n; ++i) {
    if (!has_model(entities_[i], ModelKind::Sensor)) {
      continue;
    }
    sensor_idx.push_back(i);
    const cw::scenario::ModelMountDesc* sm = Engine::find_mount(entities_[i], ModelKind::Sensor);
    const float r = sm ? param_float(*sm, "range_m", 30000.F) : 30000.F;
    if (r > max_range_m && std::isfinite(r)) {
      max_range_m = r;
    }
  }
  if (sensor_idx.empty()) {
    return;
  }
  if (max_range_m < 1.F) {
    max_range_m = 1.F;
  }

  auto consider_pair = [&](const EntityRecord& obs, const EntityRecord& tgt, float range_m, float fov_deg,
                           const cw::math::Vec3& fwd) {
    if (tgt.id == obs.id) {
      return;
    }
    const cw::math::Vec3 d = tgt.kin.position - obs.kin.position;
    const float dist2 = cw::math::dot(d, d);
    const float range2 = range_m * range_m;
    if (!(dist2 <= range2) || !std::isfinite(dist2)) {
      return;
    }
    const float dist = std::sqrt(dist2);
    if (!in_sensor_fov_delta(fwd, d, dist, fov_deg)) {
      return;
    }
    SensorDetection det;
    det.observer_id = obs.id;
    det.target_id = tgt.id;
    det.range_m = dist;
    det.reported_rcs_m2 = tgt.signature_cache.radar_rcs_m2;
    snapshot_.sensor_detections.push_back(det);
  };

  constexpr std::size_t kSpatialHashThreshold = 192;
  if (n < kSpatialHashThreshold) {
    for (const std::size_t oi : sensor_idx) {
      const EntityRecord& obs = entities_[oi];
      const cw::scenario::ModelMountDesc* sm = Engine::find_mount(obs, ModelKind::Sensor);
      const float range_m = sm ? param_float(*sm, "range_m", 30000.F) : 30000.F;
      const float fov_deg = sm ? param_float(*sm, "fov_deg", 360.F) : 360.F;
      const cw::math::Vec3 fwd = cw::ecs::EntityCoordinateSystem::body_forward_world_mercator(
          obs.kin.yaw_deg, obs.kin.pitch_deg, obs.kin.roll_deg);
      for (std::size_t ti = 0; ti < n; ++ti) {
        consider_pair(obs, entities_[ti], range_m, fov_deg, fwd);
      }
    }
    return;
  }

  const float cell = std::max(100.F, std::min(max_range_m, 1.0e7F));
  std::unordered_map<std::uint64_t, std::vector<std::size_t>> buckets;
  buckets.reserve(n / 2U + 1U);
  for (std::size_t i = 0; i < n; ++i) {
    const cw::math::Vec3& p = entities_[i].kin.position;
    const int ix = static_cast<int>(std::floor(static_cast<double>(p.x / cell)));
    const int iy = static_cast<int>(std::floor(static_cast<double>(p.y / cell)));
    buckets[pack_cell_xy(static_cast<std::int32_t>(ix), static_cast<std::int32_t>(iy))].push_back(i);
  }

  for (const std::size_t oi : sensor_idx) {
    const EntityRecord& obs = entities_[oi];
    const cw::scenario::ModelMountDesc* sm = Engine::find_mount(obs, ModelKind::Sensor);
    const float range_m = sm ? param_float(*sm, "range_m", 30000.F) : 30000.F;
    const float fov_deg = sm ? param_float(*sm, "fov_deg", 360.F) : 360.F;
    const cw::math::Vec3 fwd = cw::ecs::EntityCoordinateSystem::body_forward_world_mercator(
        obs.kin.yaw_deg, obs.kin.pitch_deg, obs.kin.roll_deg);

    const cw::math::Vec3& op = obs.kin.position;
    const int ix0 = static_cast<int>(std::floor(static_cast<double>(op.x / cell)));
    const int iy0 = static_cast<int>(std::floor(static_cast<double>(op.y / cell)));
    const int ir =
        static_cast<int>(std::ceil(static_cast<double>(range_m / cell))) + 1;

    for (int dy = -ir; dy <= ir; ++dy) {
      for (int dx = -ir; dx <= ir; ++dx) {
        const std::uint64_t key =
            pack_cell_xy(static_cast<std::int32_t>(ix0 + dx), static_cast<std::int32_t>(iy0 + dy));
        const auto it = buckets.find(key);
        if (it == buckets.end()) {
          continue;
        }
        for (const std::size_t ti : it->second) {
          consider_pair(obs, entities_[ti], range_m, fov_deg, fwd);
        }
      }
    }
  }
}

}  // namespace cw::engine
