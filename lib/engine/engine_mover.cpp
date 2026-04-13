#include "cw/engine/engine.hpp"
#include "cw/engine/engine_detail.hpp"

#include "cw/math/constants.hpp"
#include "cw/motion/motion_model.hpp"
#include "cw/vec3.hpp"

#include <cmath>
#include <string>

namespace cw::engine {

namespace {

constexpr float kWaypointArriveM = 10.F;

}  // namespace

void Engine::run_mover_step(float dt) {
  using cw::engine::detail::param_float;
  using cw::engine::detail::param_str;

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

}  // namespace cw::engine
