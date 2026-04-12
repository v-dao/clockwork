#pragma once

#include <cstdint>

namespace cw::engine {

enum class EngineState : std::uint8_t {
  Uninitialized,
  Ready,
  Running,
  Paused,
  Stopped,
};

enum class ModelKind : std::uint8_t {
  Mover = 0,
  Sensor,
  Comdevice,
  Processor,
  Weapon,
  Signature,
};

using EntityId = std::uint64_t;

const char* engine_state_name(EngineState s) noexcept;
const char* model_kind_name(ModelKind k) noexcept;

}  // namespace cw::engine
