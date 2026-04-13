#pragma once

#include "cw/model_kind.hpp"

#include <cstdint>

namespace cw::engine {

enum class EngineState : std::uint8_t {
  Uninitialized,
  Ready,
  Running,
  Paused,
  Stopped,
};

using ModelKind = cw::ModelKind;

using EntityId = std::uint64_t;

const char* engine_state_name(EngineState s) noexcept;
const char* model_kind_name(ModelKind k) noexcept;

}  // namespace cw::engine
