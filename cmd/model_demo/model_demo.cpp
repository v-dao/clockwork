#include "cw/engine/engine.hpp"
#include "cw/log.hpp"
#include "cw/scenario/parse.hpp"

#include <cstdlib>
#include <string>

namespace {

void check(cw::Error e, const char* what) {
  if (!cw::ok(e)) {
    cw::log(cw::LogLevel::Error, std::string("model_demo: failed: ").append(what));
    std::exit(EXIT_FAILURE);
  }
}

std::string entity_name(const cw::engine::SituationSnapshot& snap, cw::engine::EntityId id) {
  for (const auto& e : snap.entities) {
    if (e.id == id) {
      return e.name;
    }
  }
  return "?";
}

}  // namespace

int main(int argc, char** argv) {
  const char* path = "scenarios/model_test.cws";
  if (argc >= 2 && argv[1] != nullptr && argv[1][0] != '\0') {
    path = argv[1];
  }

  cw::scenario::Scenario sc{};
  check(cw::scenario::parse_scenario_file(path, sc), "parse_scenario_file");

  cw::engine::Engine engine;
  engine.set_fixed_step(0.05);
  check(engine.initialize(), "initialize");
  check(engine.apply_scenario(sc), "apply_scenario");
  check(engine.start(), "start");

  const auto& snap0 = engine.situation();
  cw::log(cw::LogLevel::Info,
          std::string("model_demo: t=0 detections=") + std::to_string(snap0.sensor_detections.size()));
  if (snap0.sensor_detections.empty()) {
    cw::log(cw::LogLevel::Error, "model_demo: expected at least one detection at t=0");
    return EXIT_FAILURE;
  }
  for (const auto& d : snap0.sensor_detections) {
    cw::log(cw::LogLevel::Info,
            std::string(" ") + entity_name(snap0, d.observer_id) + " -> " +
                entity_name(snap0, d.target_id) + " range_m=" + std::to_string(d.range_m) +
                " rcs_m2=" + std::to_string(d.reported_rcs_m2));
  }

  for (int i = 0; i < 20; ++i) {
    engine.step();
  }
  const auto& snap1 = engine.situation();
  cw::log(cw::LogLevel::Info,
          std::string("model_demo: after 20 steps sim_time=") + std::to_string(snap1.sim_time) +
              " detections=" + std::to_string(snap1.sensor_detections.size()));

  for (const auto& e : snap1.entities) {
    if (e.name == "hunter") {
      cw::log(cw::LogLevel::Info,
              std::string("model_demo: hunter pos x=") + std::to_string(e.position.x) +
                  " y=" + std::to_string(e.position.y) + " z=" + std::to_string(e.position.z));
    }
  }

  check(engine.end(), "end");
  cw::log(cw::LogLevel::Info, "model_demo: ok");
  return EXIT_SUCCESS;
}
