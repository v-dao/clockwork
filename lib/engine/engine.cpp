#include "cw/engine/engine.hpp"

#include "cw/ecs/entity_coordinate_system.hpp"
#include "cw/log.hpp"
#include "cw/math/constants.hpp"
#include "cw/motion/motion_model.hpp"
#include "cw/string_match.hpp"
#include "cw/vec3.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace cw::engine {

namespace {

/// 每步内模型调度顺序（**不含** `Sensor`）。
/// `Sensor` 在 `aggregate_situation()` 末尾通过 `run_model_pass(Sensor)` 执行：此时本帧 `Mover` 已更新位姿，
/// 且 `step()` 在写入快照前已递增 `sim_time_`，探测列表与 `snapshot_.sim_time` 一致；仅调用 `aggregate_situation`
///（如 `apply_scenario` / `start`）时也会走同一通道刷新探测。
constexpr ModelKind kModelPassOrder[] = {ModelKind::Comdevice, ModelKind::Processor, ModelKind::Weapon,
                                         ModelKind::Signature, ModelKind::Mover};

constexpr float kWaypointArriveM = 10.F;

void log_engine_fail(const char* operation, cw::Error err, EngineState s) {
  char ctx[144];
  std::snprintf(ctx, sizeof(ctx), "%s(state=%s)", operation, engine_state_name(s));
  cw::log_error(ctx, err);
}

std::string param_str(const cw::scenario::ModelMountDesc& m, const char* key) {
  for (const auto& p : m.params) {
    if (cw::ieq(p.first, key)) {
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

constexpr std::uint64_t pack_cell_xy(int32_t ix, int32_t iy) noexcept {
  return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(ix)) << 32) |
         static_cast<std::uint32_t>(iy);
}

}  // namespace

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
    log_engine_fail("Engine::initialize", Error::WrongState, state_);
    return Error::WrongState;
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
    log_engine_fail("Engine::start", Error::WrongState, state_);
    return Error::WrongState;
  }
  state_ = EngineState::Running;
  aggregate_situation();
  return Error::Ok;
}

Error Engine::pause() {
  if (state_ != EngineState::Running) {
    log_engine_fail("Engine::pause", Error::WrongState, state_);
    return Error::WrongState;
  }
  state_ = EngineState::Paused;
  aggregate_situation();
  return Error::Ok;
}

Error Engine::end() {
  if (state_ == EngineState::Uninitialized) {
    log_engine_fail("Engine::end", Error::WrongState, state_);
    return Error::WrongState;
  }
  state_ = EngineState::Stopped;
  aggregate_situation();
  return Error::Ok;
}

Error Engine::set_time_scale(double scale) {
  if (!(scale > 0.0) || !std::isfinite(scale)) {
    log_engine_fail("Engine::set_time_scale", Error::InvalidArgument, state_);
    return Error::InvalidArgument;
  }
  time_scale_ = scale;
  aggregate_situation();
  return Error::Ok;
}

Error Engine::save_snapshot() {
  if (federated_) {
    cw::log_error("Engine::save_snapshot(federated=true)", Error::NotAllowedWhenFederated);
    return Error::NotAllowedWhenFederated;
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
    cw::log_error("Engine::restore_snapshot(federated=true)", Error::NotAllowedWhenFederated);
    return Error::NotAllowedWhenFederated;
  }
  if (!checkpoint_.has_value()) {
    log_engine_fail("Engine::restore_snapshot", Error::NoSnapshot, state_);
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
    log_engine_fail("Engine::apply_scenario", Error::WrongState, state_);
    return Error::WrongState;
  }
  if (sc.version != 1 && sc.version != 2) {
    cw::log_error("Engine::apply_scenario(unsupported scenario version)", Error::UnsupportedScenarioVersion);
    return Error::UnsupportedScenarioVersion;
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
    rec.assembly.external_id = e.external_id;
    rec.assembly.name = e.name;
    rec.assembly.faction = e.faction;
    rec.assembly.variant_ref = e.variant_ref;
    rec.assembly.icon_2d_path = e.icon_2d_path;
    rec.assembly.model_3d_path = e.model_3d_path;
    rec.assembly.has_display_color = e.has_display_color;
    rec.assembly.display_color_r = e.display_color_r;
    rec.assembly.display_color_g = e.display_color_g;
    rec.assembly.display_color_b = e.display_color_b;
    rec.assembly.platform_attributes = e.platform_attributes;
    rec.kin.position = e.position;
    rec.kin.yaw_deg = e.yaw_deg;
    rec.kin.pitch_deg = e.pitch_deg;
    rec.kin.roll_deg = e.roll_deg;
    rec.kin.velocity = cw::ecs::EntityCoordinateSystem::body_velocity_to_world_mercator(
        e.velocity, e.yaw_deg, e.pitch_deg, e.roll_deg);
    rec.kin.angular_velocity = {};
    rec.assembly.mounts = e.mounts;
    rec.assembly.script = e.script;
    init_entity_runtime_fields(rec);
    entities_.push_back(std::move(rec));
  }
  aggregate_situation();
  return Error::Ok;
}

Error Engine::reset_with_scenario(const cw::scenario::Scenario& sc) {
  Error e = end();
  if (!cw::ok(e)) {
    return e;
  }
  e = initialize();
  if (!cw::ok(e)) {
    return e;
  }
  e = apply_scenario(sc);
  if (!cw::ok(e)) {
    return e;
  }
  return start();
}

Error Engine::add_entity(std::string name, std::vector<ModelKind> models) {
  if (state_ != EngineState::Ready) {
    log_engine_fail("Engine::add_entity", Error::WrongState, state_);
    return Error::WrongState;
  }
  EntityRecord rec;
  rec.id = next_id_++;
  rec.assembly.name = std::move(name);
  for (ModelKind mk : models) {
    cw::scenario::ModelMountDesc md;
    md.kind = mk;
    rec.assembly.mounts.push_back(std::move(md));
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
    const bool track_yaw =
        !mv || param_str(*mv, "track_yaw").empty() || param_str(*mv, "track_yaw") != "0";
    const bool track_pitch = mv && param_str(*mv, "track_pitch") == "1";

    cw::motion::MoverRuntimeState st{};
    st.position = e.kin.position;
    st.velocity = e.kin.velocity;
    st.yaw_deg = e.kin.yaw_deg;
    st.pitch_deg = e.kin.pitch_deg;
    st.roll_deg = e.kin.roll_deg;
    st.route_id = e.mover_cache.route_id;
    st.route_wp_index = e.mover_cache.route_wp_index;

    cw::motion::MoverStepInput in{};
    in.dt = dt;
    in.speed_cap_mps = speed_cap;
    in.waypoint_arrive_m = kWaypointArriveM;
    in.routes = &routes_;
    in.track_yaw_from_velocity = track_yaw;
    in.track_pitch_from_velocity = track_pitch;

    const cw::motion::MotionModel& model = cw::motion::motion_model_for_kind(e.mover_cache.kind);
    model.apply_dynamics(st, in);

    e.kin.velocity = st.velocity;
    e.kin.yaw_deg = st.yaw_deg;
    e.kin.pitch_deg = st.pitch_deg;
    e.kin.roll_deg = st.roll_deg;
    e.mover_cache.route_wp_index = st.route_wp_index;
    e.kin.position = e.kin.position + cw::math::scale(e.kin.velocity, dt);
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
      compute_sensor_detections();
      return;
  }
}

void Engine::compute_sensor_detections() {
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

void Engine::aggregate_situation() {
  snapshot_.sim_time = sim_time_;
  snapshot_.time_scale = time_scale_;
  snapshot_.engine_state = state_;
  snapshot_.entities.clear();
  snapshot_.entities.reserve(entities_.size());
  for (const auto& e : entities_) {
    EntitySituation s;
    s.id = e.id;
    s.external_id = e.assembly.external_id;
    s.name = e.assembly.name;
    s.faction = e.assembly.faction;
    s.variant_ref = e.assembly.variant_ref;
    s.icon_2d_path = e.assembly.icon_2d_path;
    s.model_3d_path = e.assembly.model_3d_path;
    s.has_display_color = e.assembly.has_display_color;
    s.display_color_r = e.assembly.display_color_r;
    s.display_color_g = e.assembly.display_color_g;
    s.display_color_b = e.assembly.display_color_b;
    s.position = e.kin.position;
    s.velocity = e.kin.velocity;
    s.angular_velocity = e.kin.angular_velocity;
    s.yaw_deg = e.kin.yaw_deg;
    s.pitch_deg = e.kin.pitch_deg;
    s.roll_deg = e.kin.roll_deg;
    snapshot_.entities.push_back(std::move(s));
  }
  run_model_pass(ModelKind::Sensor);
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
