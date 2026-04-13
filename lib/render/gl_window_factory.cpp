#include "cw/render/gl_window.hpp"

#if defined(_WIN32)
#include "cw/render/gl_window_win32.hpp"
#endif

namespace cw::render {

std::unique_ptr<GlWindow> create_gl_window() {
#if defined(_WIN32)
  return std::make_unique<GlWindowWin32>();
#else
  class GlWindowNull final : public GlWindow {
   public:
    ~GlWindowNull() override = default;
    [[nodiscard]] bool open(const GlWindowConfig&) override { return false; }
    void close() noexcept override {}
    void poll_events() noexcept override {}
    void swap_buffers() noexcept override {}
    void sync_client_size_from_window() noexcept override {}
  };
  return std::make_unique<GlWindowNull>();
#endif
}

}  // namespace cw::render
