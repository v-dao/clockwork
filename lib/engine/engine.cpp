#include "cw/engine/engine.hpp"

#include "cw/ecs/entity_coordinate_system.hpp"
#include "cw/log.hpp"

#include <cmath>
#include <cstdio>
#include <utility>

namespace cw::engine {

namespace {

/// 每步内模型调度顺序（**不含** `Sensor`）。
/// `Sensor` 在 `aggregate_situation()` 末尾通过 `run_model_pass(Sensor)` 执行：此时本帧 `Mover` 已更新位姿，
/// 且 `step()` 在写入快照前已递增 `sim_time_`，探测列表与 `snapshot_.sim_time` 一致。
/// `start` / `pause` / `end` / `set_time_scale` 在实体未变时走 `patch_situation_meta()`，不重拷实体、不重算传感器。
constexpr ModelKind kModelPassOrder[] = {ModelKind::Comdevice, ModelKind::Processor, ModelKind::Weapon,
                                         ModelKind::Signature, ModelKind::Mover};

void log_engine_fail(const char* operation, cw::Error err, EngineState s) {
  char ctx[144];
  std::snprintf(ctx, sizeof(ctx), "%s(state=%s)", operation, engine_state_name(s));
  cw::log_error(ctx, err);
}

}  // namespace

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
  patch_situation_meta();
  return Error::Ok;
}

Error Engine::pause() {
  if (state_ != EngineState::Running) {
    log_engine_fail("Engine::pause", Error::WrongState, state_);
    return Error::WrongState;
  }
  state_ = EngineState::Paused;
  patch_situation_meta();
  return Error::Ok;
}

Error Engine::end() {
  if (state_ == EngineState::Uninitialized) {
    log_engine_fail("Engine::end", Error::WrongState, state_);
    return Error::WrongState;
  }
  state_ = EngineState::Stopped;
  patch_situation_meta();
  return Error::Ok;
}

Error Engine::set_time_scale(double scale) {
  if (!(scale > 0.0) || !std::isfinite(scale)) {
    log_engine_fail("Engine::set_time_scale", Error::InvalidArgument, state_);
    return Error::InvalidArgument;
  }
  time_scale_ = scale;
  patch_situation_meta();
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

void Engine::patch_situation_meta() {
  snapshot_.sim_time = sim_time_;
  snapshot_.time_scale = time_scale_;
  snapshot_.engine_state = state_;
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

void Engine::aggregate_situation() {
  patch_situation_meta();
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
