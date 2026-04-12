#pragma once

#include "cw/engine/situation.hpp"
#include "cw/error.hpp"
#include "cw/scenario/scenario.hpp"

#include <optional>
#include <string>
#include <vector>

namespace cw::engine {

class Engine {
 public:
  Engine();

  void set_fixed_step(double dt_seconds);
  [[nodiscard]] double fixed_step() const noexcept { return fixed_dt_; }

  [[nodiscard]] Error initialize();

  [[nodiscard]] Error start();
  [[nodiscard]] Error pause();
  [[nodiscard]] Error end();

  [[nodiscard]] Error set_time_scale(double scale);

  [[nodiscard]] double time_scale() const noexcept { return time_scale_; }
  [[nodiscard]] double sim_time() const noexcept { return sim_time_; }
  [[nodiscard]] EngineState state() const noexcept { return state_; }

  void set_federated(bool enabled) noexcept { federated_ = enabled; }
  [[nodiscard]] bool federated() const noexcept { return federated_; }

  [[nodiscard]] Error save_snapshot();
  [[nodiscard]] Error restore_snapshot();

  [[nodiscard]] Error apply_scenario(const cw::scenario::Scenario& sc);

  /// 结束当前运行 → 清空 → 重新装载想定 → `start()`；`sim_time` 归零，倍速与固定步长不变。
  [[nodiscard]] Error reset_with_scenario(const cw::scenario::Scenario& sc);

  [[nodiscard]] Error add_entity(std::string name, std::vector<ModelKind> models = {});

  [[nodiscard]] std::size_t entity_count() const noexcept { return entities_.size(); }

  void step();

  [[nodiscard]] const SituationSnapshot& situation() const noexcept { return snapshot_; }

  /// 想定静态图层与通信网（阶段 2 运行时副本，供显示/模型后续消费）。
  [[nodiscard]] const std::vector<cw::scenario::ScenarioRoute>& routes() const noexcept {
    return routes_;
  }
  [[nodiscard]] const std::vector<cw::scenario::ScenarioAirspace>& airspaces() const noexcept {
    return airspaces_;
  }
  [[nodiscard]] const std::vector<cw::scenario::CommNodeDesc>& comm_nodes() const noexcept {
    return comm_nodes_;
  }
  [[nodiscard]] const std::vector<cw::scenario::CommLinkDesc>& comm_links() const noexcept {
    return comm_links_;
  }

 private:
  struct EntityRecord {
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
    std::vector<std::pair<std::string, std::string>> platform_attributes;
    std::vector<cw::scenario::ModelMountDesc> mounts;
    std::optional<cw::scenario::ScriptBindingDesc> script;
    cw::math::Vec3 position{};
    cw::math::Vec3 velocity{};
    cw::math::Vec3 angular_velocity{};
    float yaw_deg = 0.F;
    float pitch_deg = 0.F;
    float roll_deg = 0.F;
    /// 来自 signature 挂载参数 `rcs_m2`，供 sensor MVP。
    float radar_rcs_m2 = 10.F;
    /// mover 参数 `route=<route_id>`，空表示不跟航线。
    std::string mover_route_id;
    std::size_t mover_wp_index = 0;
    /// `entity_mparam mover kind`（或 `pattern`），如 `3dof`、`stub`；默认三自由度。
    std::string mover_kind;
  };

  static bool has_model(const EntityRecord& e, ModelKind k);
  static const cw::scenario::ModelMountDesc* find_mount(const EntityRecord& e, ModelKind k);

  void pre_tick();
  void run_model_pass(ModelKind k);
  void run_mover_step(float dt);
  void compute_sensor_detections();
  void init_entity_runtime_fields(EntityRecord& rec);
  void aggregate_situation();

  struct StateCheckpoint {
    double fixed_dt{};
    double time_scale{};
    double sim_time{};
    EngineState state{};
    EntityId next_id{};
    std::vector<EntityRecord> entities{};
    std::vector<cw::scenario::ScenarioRoute> routes{};
    std::vector<cw::scenario::ScenarioAirspace> airspaces{};
    std::vector<cw::scenario::CommNodeDesc> comm_nodes{};
    std::vector<cw::scenario::CommLinkDesc> comm_links{};
  };

  std::optional<StateCheckpoint> checkpoint_;
  bool federated_ = false;

  double fixed_dt_;
  double time_scale_;
  double sim_time_;
  EngineState state_;
  EntityId next_id_;
  std::vector<EntityRecord> entities_;
  std::vector<cw::scenario::ScenarioRoute> routes_;
  std::vector<cw::scenario::ScenarioAirspace> airspaces_;
  std::vector<cw::scenario::CommNodeDesc> comm_nodes_;
  std::vector<cw::scenario::CommLinkDesc> comm_links_;
  SituationSnapshot snapshot_;
};

}  // namespace cw::engine
