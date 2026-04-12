#include "cw/engine/types.hpp"

namespace cw::engine {

const char* engine_state_name(EngineState s) noexcept {
  switch (s) {
    case EngineState::Uninitialized:
      return "Uninitialized";
    case EngineState::Ready:
      return "Ready";
    case EngineState::Running:
      return "Running";
    case EngineState::Paused:
      return "Paused";
    case EngineState::Stopped:
      return "Stopped";
  }
  return "?";
}

const char* model_kind_name(ModelKind k) noexcept {
  switch (k) {
    case ModelKind::Mover:
      return "mover";
    case ModelKind::Sensor:
      return "sensor";
    case ModelKind::Comdevice:
      return "comdevice";
    case ModelKind::Processor:
      return "processor";
    case ModelKind::Weapon:
      return "weapon";
    case ModelKind::Signature:
      return "signature";
  }
  return "?";
}

}  // namespace cw::engine
