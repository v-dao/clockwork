#include "cw/engine/engine.hpp"
#include "cw/engine/engine_detail.hpp"

#include "cw/string_match.hpp"

#include <cmath>
#include <cstdlib>
#include <string>
#include <utility>

namespace cw::engine::detail {

std::string param_str(const cw::scenario::ModelMountDesc& m, const char* key) {
  for (const auto& p : m.params) {
    if (cw::ieq(p.first, key)) {
      return p.second;
    }
  }
  return {};
}

float param_float(const cw::scenario::ModelMountDesc& m, const char* key, float fallback) {
  const std::string s = param_str(m, key);
  if (s.empty()) {
    return fallback;
  }
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || !std::isfinite(v)) {
    return fallback;
  }
  return static_cast<float>(v);
}

}  // namespace cw::engine::detail

namespace cw::engine {

bool Engine::has_model(const EntityRecord& e, ModelKind k) {
  for (const auto& m : e.assembly.mounts) {
    if (m.kind == k) {
      return true;
    }
  }
  return false;
}

const cw::scenario::ModelMountDesc* Engine::find_mount(const EntityRecord& e, ModelKind k) {
  for (const auto& m : e.assembly.mounts) {
    if (m.kind == k) {
      return &m;
    }
  }
  return nullptr;
}

void Engine::init_entity_runtime_fields(EntityRecord& rec) {
  using cw::engine::detail::param_float;
  using cw::engine::detail::param_str;

  rec.signature_cache.radar_rcs_m2 = 10.F;
  rec.mover_cache.route_id.clear();
  rec.mover_cache.route_wp_index = 0;
  rec.mover_cache.kind.clear();
  rec.comdevice_cache = {};
  rec.weapon_cache = {};

  if (const auto* sig = Engine::find_mount(rec, ModelKind::Signature)) {
    rec.signature_cache.radar_rcs_m2 = param_float(*sig, "rcs_m2", 10.F);
  }
  if (const auto* mv = Engine::find_mount(rec, ModelKind::Mover)) {
    rec.mover_cache.route_id = param_str(*mv, "route");
    std::string mk = param_str(*mv, "kind");
    if (mk.empty()) {
      mk = param_str(*mv, "pattern");
    }
    rec.mover_cache.kind = mk.empty() ? std::string("3dof") : std::move(mk);
  }
  if (const auto* cd = Engine::find_mount(rec, ModelKind::Comdevice)) {
    rec.comdevice_cache.bound_node_id = param_str(*cd, "node_id");
  }
  if (const auto* wp = Engine::find_mount(rec, ModelKind::Weapon)) {
    float rounds = param_float(*wp, "rounds", 0.F);
    if (rounds < 0.F || !std::isfinite(rounds)) {
      rounds = 0.F;
    }
    rec.weapon_cache.rounds_ready = static_cast<std::size_t>(rounds);
    const float mag = param_float(*wp, "magazine", -1.F);
    if (mag >= 0.F && std::isfinite(mag) && rec.weapon_cache.rounds_ready == 0) {
      rec.weapon_cache.rounds_ready = static_cast<std::size_t>(mag);
    }
  }
}

}  // namespace cw::engine
