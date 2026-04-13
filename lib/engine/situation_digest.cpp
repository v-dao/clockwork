#include "cw/engine/situation_digest.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cw::engine {
namespace {

constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;

std::uint64_t fnv1a_append(std::uint64_t h, const void* data, std::size_t n) noexcept {
  const auto* p = static_cast<const std::uint8_t*>(data);
  for (std::size_t i = 0; i < n; ++i) {
    h ^= static_cast<std::uint64_t>(p[i]);
    h *= kFnvPrime;
  }
  return h;
}

void mix_u8(std::uint64_t& h, std::uint8_t v) noexcept { h = fnv1a_append(h, &v, sizeof v); }

void mix_u64(std::uint64_t& h, std::uint64_t v) noexcept { h = fnv1a_append(h, &v, sizeof v); }

void mix_f32(std::uint64_t& h, float v) noexcept { h = fnv1a_append(h, &v, sizeof v); }

void mix_f64(std::uint64_t& h, double v) noexcept { h = fnv1a_append(h, &v, sizeof v); }

void mix_str(std::uint64_t& h, const std::string& s) noexcept {
  const std::uint64_t len = static_cast<std::uint64_t>(s.size());
  mix_u64(h, len);
  if (!s.empty()) {
    h = fnv1a_append(h, s.data(), s.size());
  }
}

void mix_vec3(std::uint64_t& h, const cw::math::Vec3& v) noexcept {
  mix_f32(h, v.x);
  mix_f32(h, v.y);
  mix_f32(h, v.z);
}

}  // namespace

std::uint64_t situation_digest(const SituationSnapshot& snap) noexcept {
  std::uint64_t h = kFnvOffset;
  mix_f64(h, snap.sim_time);
  mix_f64(h, snap.time_scale);
  mix_u8(h, static_cast<std::uint8_t>(snap.engine_state));

  std::vector<const EntitySituation*> ents;
  ents.reserve(snap.entities.size());
  for (const auto& e : snap.entities) {
    ents.push_back(&e);
  }
  std::sort(ents.begin(), ents.end(),
            [](const EntitySituation* a, const EntitySituation* b) { return a->id < b->id; });

  mix_u64(h, static_cast<std::uint64_t>(ents.size()));
  for (const EntitySituation* p : ents) {
    const EntitySituation& e = *p;
    mix_u64(h, e.id);
    mix_str(h, e.external_id);
    mix_str(h, e.name);
    mix_str(h, e.faction);
    mix_str(h, e.variant_ref);
    mix_str(h, e.icon_2d_path);
    mix_str(h, e.model_3d_path);
    mix_u8(h, static_cast<std::uint8_t>(e.has_display_color ? 1 : 0));
    mix_f32(h, e.display_color_r);
    mix_f32(h, e.display_color_g);
    mix_f32(h, e.display_color_b);
    mix_vec3(h, e.position);
    mix_vec3(h, e.velocity);
    mix_vec3(h, e.angular_velocity);
    mix_f32(h, e.yaw_deg);
    mix_f32(h, e.pitch_deg);
    mix_f32(h, e.roll_deg);
  }

  std::vector<SensorDetection> dets = snap.sensor_detections;
  std::sort(dets.begin(), dets.end(), [](const SensorDetection& a, const SensorDetection& b) {
    if (a.observer_id != b.observer_id) {
      return a.observer_id < b.observer_id;
    }
    if (a.target_id != b.target_id) {
      return a.target_id < b.target_id;
    }
    const std::uint32_t ar = std::bit_cast<std::uint32_t>(a.range_m);
    const std::uint32_t br = std::bit_cast<std::uint32_t>(b.range_m);
    if (ar != br) {
      return ar < br;
    }
    return std::bit_cast<std::uint32_t>(a.reported_rcs_m2) <
           std::bit_cast<std::uint32_t>(b.reported_rcs_m2);
  });

  mix_u64(h, static_cast<std::uint64_t>(dets.size()));
  for (const SensorDetection& d : dets) {
    mix_u64(h, d.observer_id);
    mix_u64(h, d.target_id);
    mix_f32(h, d.range_m);
    mix_f32(h, d.reported_rcs_m2);
  }
  return h;
}

}  // namespace cw::engine
