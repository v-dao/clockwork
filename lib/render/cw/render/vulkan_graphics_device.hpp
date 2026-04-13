#pragma once

#include "cw/render/graphics_device.hpp"

#include <memory>

namespace cw::render {

class GlWindow;

/// Win32 + Vulkan：交换链呈现；场景由离屏 OpenGL 绘制后经 `upload_swapchain_from_cpu_bgra` 上传。
class VulkanGraphicsDevice final : public GraphicsDevice {
 public:
  explicit VulkanGraphicsDevice(GlWindow& window);
  ~VulkanGraphicsDevice() override;

  VulkanGraphicsDevice(const VulkanGraphicsDevice&) = delete;
  VulkanGraphicsDevice& operator=(const VulkanGraphicsDevice&) = delete;

  /// 创建 `VkInstance` / 交换链等；失败返回 false（调用方可回退 OpenGL）。
  [[nodiscard]] bool initialize() noexcept;

  [[nodiscard]] GraphicsApi api() const noexcept override { return GraphicsApi::Vulkan; }

  void make_current() noexcept override {}
  void set_viewport(int x, int y, int width, int height) noexcept override;
  void begin_frame() noexcept override;
  void end_frame() noexcept override;
  void present() noexcept override;
  void upload_swapchain_from_cpu_bgra(int width, int height, std::size_t row_bytes,
                                      const unsigned char* pixels) noexcept override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
  GlWindow& window_;
};

}  // namespace cw::render
