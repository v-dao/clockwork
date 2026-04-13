#pragma once

#include "cw/render/graphics_device.hpp"

namespace cw::render {

class GlGraphicsDevice final : public GraphicsDevice {
 public:
  explicit GlGraphicsDevice(GlWindow& window) noexcept : window_(window) {}

  [[nodiscard]] GraphicsApi api() const noexcept override { return GraphicsApi::OpenGL; }

  void make_current() noexcept override;
  void set_viewport(int x, int y, int width, int height) noexcept override;
  void present() noexcept override;

 private:
  GlWindow& window_;
};

}  // namespace cw::render
