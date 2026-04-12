#include "cw/engine/engine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>
#include <cstdlib>
#include <utility>

namespace cw::engine {

namespace {

/// 阶段 3：与架构一致的调度；mover 在 sensor 之前，便于同帧用更新后位姿做探测。
constexpr ModelKind kModelPassOrder[] = {ModelKind::Comdevice, ModelKind::Processor, ModelKind::Weapon,
                                         ModelKind::Signature, ModelKind::Mover, ModelKind::Sensor};

constexpr float kWaypointArriveM = 10.F;

bool key_ieq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

std::string param_str(const cw::scenario::ModelMountDesc& m, const char* key) {
  for (const auto& p : m.params) {
    if (key_ieq(p.first, key)) {
      return p.second;
    }
  }
  return {};
}

float param_float(const cw::scenario::ModelMountDesc& m, const char* key, float def) {
  const std::string s = param_str(m, key);
  if (s.empty()) {
    return def;
  }
  char* end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  if (end == s.c_str() || !std::isfinite(v)) {
    return def;
  }
  return static_cast<float>(v);
}

cw::math::Vec3 v3_mul(cw::math::Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }

cw::math::Vec3 v3_normalize(cw::math::Vec3 a) {
  const float L = cw::math::length(a);
  if (L < 1e-6F) {
    return {1.F, 0.F, 0.F};
  }
  return v3_mul(a, 1.F / L);
}

const cw::scenario::ScenarioRoute* find_route(const std::vector<cw::scenario::ScenarioRoute>& routes,
                                              const std::string& id) {
  for (const auto& r : routes) {
    if (r.id == id) {
      return &r;
    }
  }
  return nullptr;
}

cw::math::Vec3 forward_from_velocity(cw::math::Vec3 vel) {
  if (cw::math::length(vel) < 1e-3F) {
    return {1.F, 0.F, 0.F};
  }
  return v3_normalize(vel);
}

bool in_sensor_fov(cw::math::Vec3 pos_obs, cw::math::Vec3 vel_obs, cw::math::Vec3 pos_tgt, float fov_deg) {
  if (fov_deg >= 359.F) {
    return true;
  }
  const cw::math::Vec3 forward = forward_from_velocity(vel_obs);
  cw::math::Vec3 to = pos_tgt - pos_obs;
  const float dist = cw::math::length(to);
  if (dist < 1e-3F) {
    return true;
  }
  to = v3_mul(to, 1.F / dist);
  const float c = std::clamp(cw::math::dot(forward, to), -1.F, 1.F);
  const float ang_deg = std::acos(c) * 180.F / 3.14159265F;
  return ang_deg <= fov_deg * 0.5F;
}

}  // namespace

bool Engine::has_model(const EntityRecord& e, ModelKind k) {
  for (const auto& m : e.mounts) {
    if (m.kind == k) {
      return true;
    }
  }
  return false;
}

const cw::scenario::ModelMountDesc* Engine::find_mount(const EntityRecord& e, ModelKind k) {
  for (const auto& m : e.mounts) {
    if (m.kind == k) {
      return &m;
    }
  }
  return nullptr;
}

void Engine::init_entity_runtime_fields(EntityRecord& rec) {
  rec.radar_rcs_m2 = 10.F;
  rec.mover_route_id.clear();
  rec.mover_wp_index = 0;
  if (const auto* sig = Engine::find_mount(rec, ModelKind::Signature)) {
    rec.radar_rcs_m2 = param_float(*sig, "rcs_m2", 10.F);
  }
  if (const auto* mv = Engine::find_mount(rec, ModelKind::Mover)) {
    rec.mover_route_id = param_str(*mv, "route");
  }
}

Engine::Engine()
    : fixed_dt_(1.0 / 60.0),
      time_scale_(1.0),
      sim_time_(0.0),
      state_(EngineState::Uninitialized),
      next_id_(1) {}

void Engine::set_fixed_step(double dt_seconds) {
  if (dt_seconds > 0.0 && std::isfinite(dt_seconds)) {
    fixed_dt_ = dt_seconds;
  }
}

Error Engine::initialize() {
  if (state_ == EngineState::Running || state_ == EngineState::Paused) {
    return Error::InvalidArgument;
  }
  entities_.clear();
  routes_.clear();
  airspaces_.clear();
  comm_nodes_.clear();
  comm_links_.clear();
  sim_time_ = 0.0;
  next_id_ = 1;
  state_ = EngineState::Ready;
  aggregate_situation();
  return Error::Ok;
}

Error Engine::start() {
  if (state_ != EngineState::Ready && state_ != EngineState::Paused) {
    return Error::InvalidArgument;
  }
  state_ = EngineState::Running;
  aggregate_situation();
  return Error::Ok;
}

Error Engine::pause() {
  if (state_ != EngineState::Running) {
    return Error::InvalidArgument;
  }
  state_ = EngineState::Paused;
  aggregate_situation();
  return Error::Ok;
}

Error Engine::end() {
  if (state_ == EngineState::Uninitialized) {
    return Error::InvalidArgument;
  }
  state_ = EngineState::Stopped;
  aggregate_situation();
  return Error::Ok;
}

Error Engine::set_time_scale(double scale) {
  if (!(scale > 0.0) || !std::isfinite(scale)) {
    return Error::InvalidArgument;
  }
  time_scale_ = scale;
  aggregate_situation();
  return Error::Ok;
}

Error Engine::save_snapshot() {
  if (federated_) {
    return Error::InvalidArgument;
  }
  StateCheckpoint cp;
  cp.fixed_dt = fixed_dt_;
  cp.time_scale = time_scale_;
  cp.sim_time = sim_time_;
  cp.state = state_;
  cp.next_id = next_id_;
  cp.entities = entities_;
  cp.routes = routes_;
  cp.airspaces = airspaces_;
  cp.comm_nodes = comm_nodes_;
  cp.comm_links = comm_links_;
  checkpoint_ = std::move(cp);
  return Error::Ok;
}

Error Engine::restore_snapshot() {
  if (federated_) {
    return Error::InvalidArgument;
  }
  if (!checkpoint_.has_value()) {
    return Error::NoSnapshot;
  }
  const StateCheckpoint& cp = *checkpoint_;
  fixed_dt_ = cp.fixed_dt;
  time_scale_ = cp.time_scale;
  sim_time_ = cp.sim_time;
  state_ = cp.state;
  next_id_ = cp.next_id;
  entities_ = cp.entities;
  routes_ = cp.routes;
  airspaces_ = cp.airspaces;
  comm_nodes_ = cp.comm_nodes;
  comm_links_ = cp.comm_links;
  aggregate_situation();
  return Error::Ok;
}

Error Engine::apply_scenario(const cw::scenario::Scenario& sc) {
  if (state_ != EngineState::Ready) {
    return Error::InvalidArgument;
  }
  if (sc.version != 1 && sc.version != 2) {
    return Error::InvalidArgument;
  }
  entities_.clear();
  routes_ = sc.routes;
  airspaces_ = sc.airspaces;
  comm_nodes_ = sc.comm_nodes;
  comm_links_ = sc.comm_links;
  next_id_ = 1;
  for (const auto& e : sc.entities) {
    EntityRecord rec;
    rec.id = next_id_++;
    rec.external_id = e.external_id;
    rec.name = e.name;
    rec.faction = e.faction;
    rec.variant_ref = e.variant_ref;
    rec.icon_2d_path = e.icon_2d_path;
    rec.model_3d_path = e.model_3d_path;
    rec.has_display_color = e.has_display_color;
    rec.display_color_r = e.display_color_r;
    rec.display_color_g = e.display_color_g;
    rec.display_color_b = e.display_color_b;
    rec.platform_attributes = e.platform_attributes;
    rec.position = e.position;
    rec.velocity = e.velocity;
    rec.angular_velocity = {};
    rec.mounts = e.mounts;
    rec.script = e.script;
    init_entity_runtime_fields(rec);
    entities_.push_back(std::move(rec));
  }
  aggregate_situation();
  return Error::Ok;
}

Error Engine::add_entity(std::string name, std::vector<ModelKind> models) {
  if (state_ != EngineState::Ready) {
    return Error::InvalidArgument;
  }
  EntityRecord rec;
  rec.id = next_id_++;
  rec.name = std::move(name);
  for (ModelKind mk : models) {
    cw::scenario::ModelMountDesc md;
    md.kind = mk;
    rec.mounts.push_back(std::move(md));
  }
  init_entity_runtime_fields(rec);
  entities_.push_back(std::move(rec));
  aggregate_situation();
  return Error::Ok;
}

void Engine::pre_tick() {}

void Engine::run_mover_step(float dt) {
  for (auto& e : entities_) {
    if (!has_model(e, ModelKind::Mover)) {
      continue;
    }
    const cw::scenario::ModelMountDesc* mv = Engine::find_mount(e, ModelKind::Mover);
    const float speed_cap = mv ? param_float(*mv, "max_speed_mps", 100.F) : 100.F;

    if (!e.mover_route_id.empty()) {
      const cw::scenario::ScenarioRoute* rt = find_route(routes_, e.mover_route_id);
      if (rt && !rt->waypoints.empty()) {
        while (e.mover_wp_index < rt->waypoints.size()) {
          const auto& wp = rt->waypoints[e.mover_wp_index];
          const cw::math::Vec3 target{wp.x, wp.y, wp.z};
          const cw::math::Vec3 delta = target - e.position;
          const float dist = cw::math::length(delta);
          if (dist <= kWaypointArriveM) {
            ++e.mover_wp_index;
            continue;
          }
          e.velocity = v3_mul(v3_normalize(delta), speed_cap);
          break;
        }
      }
    }

    e.position = e.position + v3_mul(e.velocity, dt);
  }
}

void Engine::run_model_pass(ModelKind k) {
  switch (k) {
    case ModelKind::Comdevice:
    case ModelKind::Processor:
    case ModelKind::Weapon:
      return;
    case ModelKind::Signature:
      return;
    case ModelKind::Mover: {
      const float dt = static_cast<float>(fixed_dt_ * time_scale_);
      run_mover_step(dt);
      return;
    }
    case ModelKind::Sensor:
      return;
  }
}

void Engine::compute_sensor_detections() {
  snapshot_.sensor_detections.clear();

  for (const auto& obs : entities_) {
    if (!has_model(obs, ModelKind::Sensor)) {
      continue;
    }
    const cw::scenario::ModelMountDesc* sm = Engine::find_mount(obs, ModelKind::Sensor);
    const float range_m = sm ? param_float(*sm, "range_m", 30000.F) : 30000.F;
    const float fov_deg = sm ? param_float(*sm, "fov_deg", 360.F) : 360.F;

    for (const auto& tgt : entities_) {
      if (tgt.id == obs.id) {
        continue;
      }
      const cw::math::Vec3 d = tgt.position - obs.position;
      const float dist = cw::math::length(d);
      if (dist > range_m || !std::isfinite(dist)) {
        continue;
      }
      if (!in_sensor_fov(obs.position, obs.velocity, tgt.position, fov_deg)) {
        continue;
      }
      SensorDetection det;
      det.observer_id = obs.id;
      det.target_id = tgt.id;
      det.range_m = dist;
      det.reported_rcs_m2 = tgt.radar_rcs_m2;
      snapshot_.sensor_detections.push_back(det);
    }
  }
}

void Engine::aggregate_situation() {
  snapshot_.sim_time = sim_time_;
  snapshot_.time_scale = time_scale_;
  snapshot_.engine_state = state_;
  snapshot_.entities.clear();
  snapshot_.entities.reserve(entities_.size());
  for (const auto& e : entities_) {
    EntitySituation s;
    s.id = e.id;
    s.external_id = e.external_id;
    s.name = e.name;
    s.faction = e.faction;
    s.variant_ref = e.variant_ref;
    s.icon_2d_path = e.icon_2d_path;
    s.model_3d_path = e.model_3d_path;
    s.has_display_color = e.has_display_color;
    s.display_color_r = e.display_color_r;
    s.display_color_g = e.display_color_g;
    s.display_color_b = e.display_color_b;
    s.position = e.position;
    s.velocity = e.velocity;
    s.angular_velocity = e.angular_velocity;
    snapshot_.entities.push_back(std::move(s));
  }
  compute_sensor_detections();
}

void Engine::step() {
  if (state_ != EngineState::Running) {
    return;
  }
  pre_tick();
  for (ModelKind k : kModelPassOrder) {
    run_model_pass(k);
  }
  sim_time_ += fixed_dt_ * time_scale_;
  aggregate_situation();
}

}  // namespace cw::engine
