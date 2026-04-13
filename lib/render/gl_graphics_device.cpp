#include "cw/render/gl_graphics_device.hpp"

#include "cw/render/gl_window.hpp"

#include <GL/gl.h>

namespace cw::render {

void GlGraphicsDevice::make_current() noexcept { window_.make_current(); }

void GlGraphicsDevice::set_viewport(int x, int y, int width, int height) noexcept {
  if (width < 1) {
    width = 1;
  }
  if (height < 1) {
    height = 1;
  }
  glViewport(x, y, width, height);
}

void GlGraphicsDevice::present() noexcept { window_.swap_buffers(); }

}  // namespace cw::render
