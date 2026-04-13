#include "cw/motion/motion_model_3dof.hpp"

#include "cw/math/constants.hpp"
#include "cw/string_match.hpp"

#include <algorithm>
#include <cmath>

namespace cw::motion {

namespace {

const cw::scenario::ScenarioRoute* find_route(const std::vector<cw::scenario::ScenarioRoute>& routes,
                                              const std::string& id) {
  for (const auto& r : routes) {
    if (r.id == id) {
      return &r;
    }
  }
  return nullptr;
}

}  // namespace

void MotionModel3dof::apply_dynamics(MoverRuntimeState& state, const MoverStepInput& in) const {
  if (!state.route_id.empty() && in.routes != nullptr) {
    const cw::scenario::ScenarioRoute* rt = find_route(*in.routes, state.route_id);
    if (rt != nullptr && !rt->waypoints.empty()) {
      while (state.route_wp_index < rt->waypoints.size()) {
        const auto& wp = rt->waypoints[state.route_wp_index];
        const cw::math::Vec3 target{wp.x, wp.y, wp.z};
        const cw::math::Vec3 delta = target - state.position;
        const float dist = cw::math::length(delta);
        bool advance = false;
        if (dist <= in.waypoint_arrive_m) {
          advance = true;
        } else if (state.route_wp_index > 0) {
          // 沿上一段已飞过当前点（t>=1）且仍靠近该点：避免单步越过球半径后 delta 反向导致来回穿点、偏航振荡
          const auto& wp_prev = rt->waypoints[state.route_wp_index - 1];
          const cw::math::Vec3 prev{wp_prev.x, wp_prev.y, wp_prev.z};
          const cw::math::Vec3 seg = target - prev;
          const float seg_len2 = cw::math::dot(seg, seg);
          if (seg_len2 > 1e-12F) {
            const float t_along = cw::math::dot(state.position - prev, seg) / seg_len2;
            const float seg_len = std::sqrt(seg_len2);
            const float max_pass_dist =
                std::max(50.F * in.waypoint_arrive_m, 0.05F * seg_len);
            if (t_along >= 1.0F && dist <= max_pass_dist) {
              advance = true;
            }
          }
        } else {
          // 首点无上一顶点：用「已掠过」判据（上一帧速度与本帧指向目标的位移反向）
          if (cw::math::dot(state.velocity, delta) < 0.F && dist < 50.F * in.waypoint_arrive_m) {
            advance = true;
          }
        }
        if (advance) {
          ++state.route_wp_index;
          continue;
        }
        state.velocity = cw::math::scale(cw::math::normalize(delta), in.speed_cap_mps);
        break;
      }
      /// 已通过全部航点：不再制导，停止平移。否则仍以 cap 速度漂移，战术/地球「跟实体包络」会无限外扩、缩放持续变小。
      if (state.route_wp_index >= rt->waypoints.size()) {
        state.velocity = {};
      }
    }
  }

  if (in.track_yaw_from_velocity) {
    const float vx = state.velocity.x;
    const float vy = state.velocity.y;
    const float vh = std::sqrt(vx * vx + vy * vy);
    if (vh > 0.5F) {
      state.yaw_deg = std::atan2(vx, vy) * (180.F / cw::math::kPiF);
    }
    if (in.track_pitch_from_velocity) {
      const float vz = state.velocity.z;
      const float vm = cw::math::length(state.velocity);
      if (vm > 0.5F) {
        state.pitch_deg = std::asin(std::clamp(vz / vm, -1.F, 1.F)) * (180.F / cw::math::kPiF);
      }
    }
  }
}

namespace {

MotionModel3dof g_model_3dof{};
MotionModelStub g_model_stub{};

}  // namespace

const MotionModel& motion_model_for_kind(const std::string& kind) {
  if (cw::ieq_cstr(kind.c_str(), "stub") || cw::ieq_cstr(kind.c_str(), "none") ||
      cw::ieq_cstr(kind.c_str(), "passive")) {
    return g_model_stub;
  }
  if (kind.empty() || cw::ieq_cstr(kind.c_str(), "3dof") ||
      cw::ieq_cstr(kind.c_str(), "three_dof")) {
    return g_model_3dof;
  }
  return g_model_3dof;
}

}  // namespace cw::motion
