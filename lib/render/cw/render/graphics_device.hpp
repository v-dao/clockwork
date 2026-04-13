#pragma once

#include "cw/render/graphics_types.hpp"

#include <cstddef>
#include <memory>

namespace cw::render {

class GlWindow;

/// 与窗口绑定的图形设备抽象：上下文绑定、视口、呈现。
///
/// **与 Vulkan 迁移的关系**：OpenGL 将「交换缓冲」与隐式全局状态合在上下文里；Vulkan 则需显式
/// `vkAcquireNextImageKHR` / `vkQueuePresentKHR` 及与 `VkRenderPass` 对齐的帧划分。本接口刻意保留
/// `begin_frame` / `end_frame` / `present` 分离，便于日后在 `VulkanGraphicsDevice` 中填入 acquire、
/// `submit`、`present`，而 `GlGraphicsDevice` 维持轻量转发。
///
/// **当前范围**：`lib/render` / `lib/situation_view` 内大量 immediate-mode `gl*` 尚未迁入设备；
/// 新代码应优先通过本类做上下文与呈现；几何与着色迁移需配合后续 `RenderPass` / 缓冲与管线资源抽象。
class GraphicsDevice {
 public:
  virtual ~GraphicsDevice();

  [[nodiscard]] virtual GraphicsApi api() const noexcept = 0;

  /// 将图形 API 上下文绑定到当前线程（Win32/WGL 等）。
  virtual void make_current() noexcept = 0;

  /// 设置光栅化视口（对应 OpenGL `glViewport`；Vulkan 中通常在动态状态或 `vkCmdSetViewport`）。
  virtual void set_viewport(int x, int y, int width, int height) noexcept = 0;

  /// 帧起点：Vulkan 侧可在此 `acquire` 交换链图像；OpenGL 默认可为空或仅作占位。
  virtual void begin_frame() noexcept {}

  /// 帧终点：Vulkan 侧可记录 `submit` 前的屏障/信号量；OpenGL 默认空操作。
  virtual void end_frame() noexcept {}

  /// 将已完成帧提交到屏幕（OpenGL 为 `SwapBuffers`；Vulkan 为 `QueuePresent`）。
  virtual void present() noexcept = 0;

  /// Vulkan：在 `begin_frame` 与 `end_frame` 之间调用，将离屏 OpenGL 读回的 BGRA8（行 `row_bytes` 字节，
  /// 通常为 `width*4`）写入当前 acquire 的交换链图像；OpenGL 实现为空操作。
  virtual void upload_swapchain_from_cpu_bgra(int width, int height, std::size_t row_bytes,
                                              const unsigned char* pixels) noexcept {
    (void)width;
    (void)height;
    (void)row_bytes;
    (void)pixels;
  }

  /// Vulkan 方案1：为 true 时每帧仅用交换链 `RenderPass` 清屏呈现，不要求离屏 GL 与 `upload_swapchain_from_cpu_bgra`。
  virtual void set_vulkan_native_scene_clear_only(bool enabled) noexcept { (void)enabled; }
  [[nodiscard]] virtual bool vulkan_native_scene_clear_only() const noexcept { return false; }
  /// 用仿真时间等调制原生清屏色（仅 `vulkan_native_scene_clear_only()` 为 true 时由 Vulkan 设备使用）。
  virtual void set_vulkan_native_scene_anim_param(float sim_time_seconds) noexcept {
    (void)sim_time_seconds;
  }
};

/// 按 `GlWindow::window_graphics_api()` 选择实现；Vulkan 初始化失败时返回 `nullptr`（可回退重开窗口为 OpenGL）。
[[nodiscard]] std::unique_ptr<GraphicsDevice> create_graphics_device_for_window(GlWindow& window);

}  // namespace cw::render
