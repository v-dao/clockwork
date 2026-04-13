#pragma once

#include <cstdint>

namespace cw {

/// 仿真挂载的模型类型（想定、引擎与工具共用；与 `cw::engine::ModelKind` 为同一枚举）。
enum class ModelKind : std::uint8_t {
  Mover = 0,
  Sensor,
  Comdevice,
  Processor,
  Weapon,
  Signature,
};

}  // namespace cw
