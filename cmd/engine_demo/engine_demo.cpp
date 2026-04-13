#include "cw/engine/engine.hpp"
#include "cw/error.hpp"
#include "cw/log.hpp"

#include <cmath>
#include <cstdlib>
#include <string>

namespace {

void check_ok(cw::Error e, const char* what) {
  if (!cw::ok(e)) {
    std::string msg = "engine_demo: failed: ";
    msg += what;
    msg += " [";
    msg += cw::error_code_str(e);
    msg += "] ";
    msg += cw::error_message(e);
    cw::log(cw::LogLevel::Error, msg);
    std::exit(EXIT_FAILURE);
  }
}

void check_fail(cw::Error e, const char* what) {
  if (cw::ok(e)) {
    cw::log(cw::LogLevel::Error, std::string("engine_demo: expected failure: ").append(what));
    std::exit(EXIT_FAILURE);
  }
}

bool nearly_equal(double a, double b) {
  return std::abs(a - b) < 1e-9;
}

}  // namespace

int main() {
  using cw::LogLevel;
  using cw::engine::Engine;
  using cw::engine::ModelKind;

  // --- Federated 占位：禁止快照 ---
  {
    Engine eng;
    eng.set_federated(true);
    check_ok(eng.initialize(), "federated init");
    check_fail(eng.save_snapshot(), "save_snapshot when federated");
    check_fail(eng.restore_snapshot(), "restore_snapshot when federated");
    cw::log(LogLevel::Info, "engine_demo: federated snapshot correctly rejected");
  }

  // --- 单机：多步 + 快照恢复 ---
  Engine engine;
  engine.set_fixed_step(0.1);
  engine.set_federated(false);

  check_ok(engine.initialize(), "initialize");
  check_ok(engine.add_entity("alpha", {ModelKind::Mover, ModelKind::Sensor}), "add_entity alpha");
  check_ok(engine.add_entity("bravo", {ModelKind::Signature}), "add_entity bravo");
  check_ok(engine.set_time_scale(2.0), "set_time_scale");

  check_ok(engine.start(), "start");

  constexpr int kSteps = 5;
  for (int i = 0; i < kSteps; ++i) {
    engine.step();
    const auto& snap = engine.situation();
    std::string msg = "engine_demo: step ";
    msg += std::to_string(i + 1);
    msg += " sim_time=";
    msg += std::to_string(snap.sim_time);
    msg += " entities=";
    msg += std::to_string(snap.entities.size());
    cw::log(LogLevel::Info, msg);
  }

  check_ok(engine.pause(), "pause");
  engine.step();  // should not advance when paused
  cw::log(LogLevel::Info,
          std::string("engine_demo: after pause sim_time=") + std::to_string(engine.sim_time()));

  // 恢复运行后推进两步并打快照
  check_ok(engine.start(), "start after pause");
  engine.step();
  engine.step();
  const double t_at_save = engine.sim_time();
  const std::size_t n_at_save = engine.entity_count();
  check_ok(engine.save_snapshot(), "save_snapshot");
  cw::log(LogLevel::Info,
          std::string("engine_demo: saved sim_time=") + std::to_string(t_at_save) + " entities=" +
              std::to_string(n_at_save));

  engine.step();
  engine.step();
  if (nearly_equal(engine.sim_time(), t_at_save)) {
    cw::log(LogLevel::Error, "engine_demo: sim_time should have advanced before restore");
    return EXIT_FAILURE;
  }

  check_ok(engine.restore_snapshot(), "restore_snapshot");
  if (!nearly_equal(engine.sim_time(), t_at_save) || engine.entity_count() != n_at_save) {
    cw::log(LogLevel::Error, "engine_demo: restore mismatch sim_time or entity_count");
    return EXIT_FAILURE;
  }
  cw::log(LogLevel::Info,
          std::string("engine_demo: after restore sim_time=") + std::to_string(engine.sim_time()) +
              " entities=" + std::to_string(engine.entity_count()));

  engine.step();
  const double t_after = engine.sim_time();
  const double expected = t_at_save + engine.fixed_step() * engine.time_scale();
  if (!nearly_equal(t_after, expected)) {
    cw::log(LogLevel::Error, "engine_demo: step after restore did not match expected delta");
    return EXIT_FAILURE;
  }
  cw::log(LogLevel::Info,
          std::string("engine_demo: one step after restore sim_time=") + std::to_string(t_after));

  check_ok(engine.end(), "end");
  cw::log(LogLevel::Info, "engine_demo: done");
  return EXIT_SUCCESS;
}
