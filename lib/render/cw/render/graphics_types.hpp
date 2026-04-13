#pragma once

#include <cstdint>

namespace cw::render {

/// 图形后端标识。新增后端时扩展此枚举并保持 `create_graphics_device_for_window` 等工厂可切换。
enum class GraphicsApi : std::uint8_t {
  OpenGL = 0,
  /// 预留：与 `GraphicsDevice` 第二套实现（交换链、命令缓冲、显式 `RenderPass`）对齐。
  Vulkan = 1,
};

}  // namespace cw::render
