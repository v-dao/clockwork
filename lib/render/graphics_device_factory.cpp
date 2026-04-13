#include "cw/render/graphics_device.hpp"

#include "cw/render/gl_graphics_device.hpp"
#include "cw/render/gl_window.hpp"
#include "cw/render/vulkan_graphics_device.hpp"
#include "cw/log.hpp"

namespace cw::render {

std::unique_ptr<GraphicsDevice> create_graphics_device_for_window(GlWindow& window) {
  if (window.window_graphics_api() == GraphicsApi::Vulkan) {
    auto v = std::make_unique<VulkanGraphicsDevice>(window);
    if (!v->initialize()) {
      cw::log(cw::LogLevel::Error,
              "Vulkan graphics device initialization failed (install Vulkan loader, driver, and dev headers; "
              "caller may fall back to OpenGL)");
      return nullptr;
    }
    return v;
  }
  return std::make_unique<GlGraphicsDevice>(window);
}

}  // namespace cw::render
