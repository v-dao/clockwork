#include "cw/engine/engine.hpp"
#include "cw/log.hpp"
#include "cw/scenario/parse.hpp"

#include <algorithm>
#include <cstdlib>
#include <string>
#include <string_view>

namespace {

void check(cw::Error e, const char* what) {
  if (!cw::ok(e)) {
    cw::log(cw::LogLevel::Error, std::string("scenario_demo: failed: ").append(what));
    std::exit(EXIT_FAILURE);
  }
}

bool snapshot_has_entity_named(const cw::engine::SituationSnapshot& snap, const std::string& name) {
  for (const auto& e : snap.entities) {
    if (e.name == name) {
      return true;
    }
  }
  return false;
}

bool entity_has_mover_mount(const cw::scenario::ScenarioEntityDesc& e) {
  for (const auto& m : e.mounts) {
    if (m.kind == cw::engine::ModelKind::Mover) {
      return true;
    }
  }
  return false;
}

void run_file(const char* path, bool expect_full) {
  using cw::LogLevel;
  using cw::engine::Engine;

  cw::scenario::Scenario sc{};
  check(cw::scenario::parse_scenario_file(path, sc), "parse_scenario_file");

  Engine engine;
  engine.set_fixed_step(0.05);
  check(engine.initialize(), "initialize");
  check(engine.apply_scenario(sc), "apply_scenario");

  const auto& snap0 = engine.situation();
  if (snap0.entities.size() != sc.entities.size()) {
    cw::log(LogLevel::Error, "scenario_demo: entity count mismatch");
    std::exit(EXIT_FAILURE);
  }
  if (!snapshot_has_entity_named(snap0, "alpha")) {
    cw::log(LogLevel::Error, "scenario_demo: missing entity alpha");
    std::exit(EXIT_FAILURE);
  }

  bool alpha_has_mover = false;
  for (const auto& e : sc.entities) {
    if (e.name == "alpha") {
      alpha_has_mover = entity_has_mover_mount(e);
      if (e.position.z != 1000.F) {
        cw::log(LogLevel::Error, "scenario_demo: alpha z mismatch");
        std::exit(EXIT_FAILURE);
      }
    }
  }
  if (!alpha_has_mover) {
    cw::log(LogLevel::Error, "scenario_demo: alpha must have mover");
    std::exit(EXIT_FAILURE);
  }

  if (expect_full) {
    if (engine.routes().size() != 1 || engine.routes()[0].waypoints.size() != 3) {
      cw::log(LogLevel::Error, "scenario_demo: full routes mismatch");
      std::exit(EXIT_FAILURE);
    }
    if (engine.airspaces().size() != 2) {
      cw::log(LogLevel::Error, "scenario_demo: full airspaces mismatch");
      std::exit(EXIT_FAILURE);
    }
    if (engine.comm_nodes().size() != 3 || engine.comm_links().size() != 2) {
      cw::log(LogLevel::Error, "scenario_demo: full comm mismatch");
      std::exit(EXIT_FAILURE);
    }
    bool found_alpha_script = false;
    for (const auto& e : sc.entities) {
      if (e.name == "alpha" && e.script.has_value() &&
          e.script->kind == cw::scenario::ScriptBindingDesc::Kind::Lua &&
          e.script->entry_symbol == "on_tick") {
        found_alpha_script = true;
      }
    }
    if (!found_alpha_script) {
      cw::log(LogLevel::Error, "scenario_demo: alpha lua script expected");
      std::exit(EXIT_FAILURE);
    }
  }

  cw::log(LogLevel::Info,
          std::string("scenario_demo: loaded ") + path + " entities=" +
              std::to_string(snap0.entities.size()));

  check(engine.start(), "start");
  engine.step();
  const auto& snap1 = engine.situation();
  cw::log(LogLevel::Info,
          std::string("scenario_demo: after step sim_time=") + std::to_string(snap1.sim_time));

  check(engine.end(), "end");
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && argv[1] != nullptr && argv[1][0] != '\0') {
    const char* path = argv[1];
    const std::string_view pv(path);
    const bool full = (pv.find("full") != std::string_view::npos);
    run_file(path, full);
  } else {
    run_file("scenarios/minimal.cws", false);
    run_file("scenarios/full.cws", true);
  }

  cw::log(cw::LogLevel::Info, "scenario_demo: all done");
  return EXIT_SUCCESS;
}
