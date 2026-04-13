// 可脚本化回归：状态机 / 快照 / 想定解析关键路径；失败非零退出。
// 请在仓库根目录运行，以便 `scenarios/full.cws` 路径有效。

#include "cw/engine/engine.hpp"
#include "cw/error.hpp"
#include "cw/log.hpp"
#include "cw/scenario/parse.hpp"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>

namespace {

bool nearly_equal(double a, double b) { return std::abs(a - b) < 1e-9; }

void fail(const char* case_name, const char* detail) {
  std::string msg = "engine_tests: FAIL [";
  msg += case_name;
  msg += "] ";
  msg += detail;
  cw::log(cw::LogLevel::Error, msg);
}

bool expect_ok(const char* case_name, cw::Error e, const char* what) {
  if (!cw::ok(e)) {
    fail(case_name, what);
    return false;
  }
  return true;
}

bool expect_error(const char* case_name, cw::Error e, cw::Error want, const char* what) {
  if (e != want) {
    fail(case_name, what);
    return false;
  }
  return true;
}

bool test_parse_minimal_inline() {
  constexpr const char* kCase = "parse_minimal_inline";
  constexpr std::string_view kText = "version 1\nentity e1 mover\n";
  cw::scenario::Scenario sc{};
  if (!expect_ok(kCase, cw::scenario::parse_scenario_text(kText, sc), "parse_scenario_text")) {
    return false;
  }
  if (sc.version != 1 || sc.entities.size() != 1 || sc.entities[0].name != "e1") {
    fail(kCase, "structure mismatch");
    return false;
  }
  return true;
}

bool test_parse_bad_version() {
  constexpr const char* kCase = "parse_bad_version";
  constexpr std::string_view kText = "version 99\nentity e1 mover\n";
  cw::scenario::Scenario sc{};
  return expect_error(kCase, cw::scenario::parse_scenario_text(kText, sc), cw::Error::ParseError,
                      "expected ParseError for version99");
}

bool test_parse_no_entities() {
  constexpr const char* kCase = "parse_no_entities";
  constexpr std::string_view kText = "version 1\n";
  cw::scenario::Scenario sc{};
  return expect_error(kCase, cw::scenario::parse_scenario_text(kText, sc), cw::Error::ParseError,
                      "expected ParseError with no entities");
}

bool test_parse_unknown_model() {
  constexpr const char* kCase = "parse_unknown_model";
  constexpr std::string_view kText = "version 1\nentity e1 not_a_real_model\n";
  cw::scenario::Scenario sc{};
  return expect_error(kCase, cw::scenario::parse_scenario_text(kText, sc), cw::Error::ParseError,
                      "expected ParseError for unknown model token");
}

bool test_parse_full_cws_file() {
  constexpr const char* kCase = "parse_full_cws_file";
  cw::scenario::Scenario sc{};
  const cw::Error e = cw::scenario::parse_scenario_file("scenarios/full.cws", sc);
  if (!expect_ok(kCase, e, "parse_scenario_file scenarios/full.cws")) {
    return false;
  }
  if (sc.version != 2 || sc.entities.size() < 2) {
    fail(kCase, "full.cws structure mismatch");
    return false;
  }
  return true;
}

bool test_engine_start_before_init() {
  constexpr const char* kCase = "engine_start_before_init";
  cw::engine::Engine eng;
  return expect_error(kCase, eng.start(), cw::Error::WrongState,
                      "start() before initialize should fail");
}

bool test_engine_pause_when_ready() {
  constexpr const char* kCase = "engine_pause_when_ready";
  cw::engine::Engine eng;
  if (!expect_ok(kCase, eng.initialize(), "initialize")) {
    return false;
  }
  return expect_error(kCase, eng.pause(), cw::Error::WrongState,
                      "pause() when Ready should fail");
}

bool test_engine_step_only_advances_when_running() {
  constexpr const char* kCase = "engine_step_only_advances_when_running";
  cw::engine::Engine eng;
  eng.set_fixed_step(0.1);
  if (!expect_ok(kCase, eng.initialize(), "initialize")) {
    return false;
  }
  eng.step();
  if (!nearly_equal(eng.sim_time(), 0.0)) {
    fail(kCase, "sim_time should not advance when not Running");
    return false;
  }
  if (!expect_ok(kCase, eng.start(), "start")) {
    return false;
  }
  eng.step();
  if (!nearly_equal(eng.sim_time(), 0.1)) {
    fail(kCase, "sim_time should advance by fixed_dt after one step");
    return false;
  }
  if (!expect_ok(kCase, eng.pause(), "pause")) {
    return false;
  }
  const double t_paused = eng.sim_time();
  eng.step();
  if (!nearly_equal(eng.sim_time(), t_paused)) {
    fail(kCase, "paused step should not advance sim_time");
    return false;
  }
  return true;
}

bool test_snapshot_restore_without_save() {
  constexpr const char* kCase = "snapshot_restore_without_save";
  cw::engine::Engine eng;
  if (!expect_ok(kCase, eng.initialize(), "initialize")) {
    return false;
  }
  return expect_error(kCase, eng.restore_snapshot(), cw::Error::NoSnapshot,
                      "restore without save should return NoSnapshot");
}

bool test_snapshot_federated_rejected() {
  constexpr const char* kCase = "snapshot_federated_rejected";
  cw::engine::Engine eng;
  eng.set_federated(true);
  if (!expect_ok(kCase, eng.initialize(), "initialize")) {
    return false;
  }
  if (!expect_error(kCase, eng.save_snapshot(), cw::Error::NotAllowedWhenFederated, "federated save")) {
    return false;
  }
  return expect_error(kCase, eng.restore_snapshot(), cw::Error::NotAllowedWhenFederated,
                      "federated restore");
}

bool test_snapshot_roundtrip() {
  constexpr const char* kCase = "snapshot_roundtrip";
  using cw::engine::Engine;
  using cw::engine::ModelKind;

  Engine engine;
  engine.set_fixed_step(0.1);
  engine.set_federated(false);
  if (!expect_ok(kCase, engine.initialize(), "initialize")) {
    return false;
  }
  if (!expect_ok(kCase, engine.add_entity("alpha", {ModelKind::Mover}), "add_entity")) {
    return false;
  }
  if (!expect_ok(kCase, engine.set_time_scale(2.0), "set_time_scale")) {
    return false;
  }
  if (!expect_ok(kCase, engine.start(), "start")) {
    return false;
  }
  for (int i = 0; i < 3; ++i) {
    engine.step();
  }
  if (!expect_ok(kCase, engine.pause(), "pause")) {
    return false;
  }
  if (!expect_ok(kCase, engine.start(), "start after pause")) {
    return false;
  }
  engine.step();
  engine.step();
  const double t_at_save = engine.sim_time();
  const std::size_t n_at_save = engine.entity_count();
  if (!expect_ok(kCase, engine.save_snapshot(), "save_snapshot")) {
    return false;
  }
  engine.step();
  engine.step();
  if (nearly_equal(engine.sim_time(), t_at_save)) {
    fail(kCase, "sim_time should advance before restore");
    return false;
  }
  if (!expect_ok(kCase, engine.restore_snapshot(), "restore_snapshot")) {
    return false;
  }
  if (!nearly_equal(engine.sim_time(), t_at_save) || engine.entity_count() != n_at_save) {
    fail(kCase, "restore mismatch");
    return false;
  }
  engine.step();
  const double expected = t_at_save + engine.fixed_step() * engine.time_scale();
  if (!nearly_equal(engine.sim_time(), expected)) {
    fail(kCase, "step after restore delta mismatch");
    return false;
  }
  return true;
}

bool test_error_catalog_strings() {
  constexpr const char* kCase = "error_catalog_strings";
  if (std::strcmp(cw::error_code_str(cw::Error::Ok), "Ok") != 0) {
    fail(kCase, "error_code_str(Ok)");
    return false;
  }
  if (cw::error_message(cw::Error::WrongState) == nullptr ||
      cw::error_message(cw::Error::WrongState)[0] == '\0') {
    fail(kCase, "error_message(WrongState)");
    return false;
  }
  return true;
}

bool test_engine_apply_unsupported_scenario_version() {
  constexpr const char* kCase = "engine_apply_unsupported_scenario_version";
  constexpr std::string_view kText = "version 1\nentity e1 mover\n";
  cw::scenario::Scenario sc{};
  if (!expect_ok(kCase, cw::scenario::parse_scenario_text(kText, sc), "parse")) {
    return false;
  }
  sc.version = 99;
  cw::engine::Engine eng;
  if (!expect_ok(kCase, eng.initialize(), "initialize")) {
    return false;
  }
  return expect_error(kCase, eng.apply_scenario(sc), cw::Error::UnsupportedScenarioVersion,
                      "apply unsupported scenario version");
}

bool test_apply_with_comdevice_weapon_mounts() {
  constexpr const char* kCase = "apply_with_comdevice_weapon_mounts";
  constexpr std::string_view kText =
      "version 1\n"
      "entity striker mover weapon comdevice\n"
      "entity_mparam striker weapon rounds 7\n"
      "entity_mparam striker comdevice node_id gw99\n";

  cw::scenario::Scenario sc{};
  if (!expect_ok(kCase, cw::scenario::parse_scenario_text(kText, sc), "parse")) {
    return false;
  }
  cw::engine::Engine eng;
  if (!expect_ok(kCase, eng.initialize(), "initialize")) {
    return false;
  }
  if (!expect_ok(kCase, eng.apply_scenario(sc), "apply_scenario")) {
    return false;
  }
  if (!expect_ok(kCase, eng.start(), "start")) {
    return false;
  }
  eng.step();
  if (eng.entity_count() != 1) {
    fail(kCase, "entity count");
    return false;
  }
  return true;
}

bool test_scenario_apply_and_step() {
  constexpr const char* kCase = "scenario_apply_and_step";
  constexpr std::string_view kText =
      "version 1\n"
      "entity alpha mover\n"
      "entity_pos alpha geo 0 0 1000\n"
      "entity bravo signature\n"
      "entity_pos bravo geo 0.1 0 500\n";

  cw::scenario::Scenario sc{};
  if (!expect_ok(kCase, cw::scenario::parse_scenario_text(kText, sc), "parse")) {
    return false;
  }
  cw::engine::Engine engine;
  engine.set_fixed_step(0.05);
  if (!expect_ok(kCase, engine.initialize(), "initialize")) {
    return false;
  }
  if (!expect_ok(kCase, engine.apply_scenario(sc), "apply_scenario")) {
    return false;
  }
  if (engine.entity_count() != 2) {
    fail(kCase, "entity count after apply");
    return false;
  }
  if (!expect_ok(kCase, engine.start(), "start")) {
    return false;
  }
  engine.step();
  if (!nearly_equal(engine.sim_time(), 0.05)) {
    fail(kCase, "sim_time after one step");
    return false;
  }
  const auto& snap = engine.situation();
  bool found_alpha = false;
  for (const auto& e : snap.entities) {
    if (e.name == "alpha" && e.position.z == 1000.F) {
      found_alpha = true;
    }
  }
  if (!found_alpha) {
    fail(kCase, "alpha position z");
    return false;
  }
  return true;
}

using TestFn = bool (*)();

struct TestEntry {
  const char* name;
  TestFn fn;
};

}  // namespace

int main() {
  static const TestEntry kTests[] = {
      {"parse_minimal_inline", test_parse_minimal_inline},
      {"parse_bad_version", test_parse_bad_version},
      {"parse_no_entities", test_parse_no_entities},
      {"parse_unknown_model", test_parse_unknown_model},
      {"parse_full_cws_file", test_parse_full_cws_file},
      {"error_catalog_strings", test_error_catalog_strings},
      {"engine_start_before_init", test_engine_start_before_init},
      {"engine_pause_when_ready", test_engine_pause_when_ready},
      {"engine_step_only_advances_when_running", test_engine_step_only_advances_when_running},
      {"snapshot_restore_without_save", test_snapshot_restore_without_save},
      {"snapshot_federated_rejected", test_snapshot_federated_rejected},
      {"snapshot_roundtrip", test_snapshot_roundtrip},
      {"engine_apply_unsupported_scenario_version", test_engine_apply_unsupported_scenario_version},
      {"apply_with_comdevice_weapon_mounts", test_apply_with_comdevice_weapon_mounts},
      {"scenario_apply_and_step", test_scenario_apply_and_step},
  };

  for (const TestEntry& t : kTests) {
    if (!t.fn()) {
      return EXIT_FAILURE;
    }
  }

  cw::log(cw::LogLevel::Info,
          std::string("engine_tests: passed ") + std::to_string(std::size(kTests)) + " cases");
  return EXIT_SUCCESS;
}
