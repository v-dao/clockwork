#pragma once

#include "cw/render/gl_offscreen_win32.hpp"
#include "cw/render/gl_window.hpp"

#include <memory>

namespace cw::render {

class GlWindowWin32 final : public GlWindow {
 public:
  ~GlWindowWin32() override;

  [[nodiscard]] bool open(const GlWindowConfig& cfg) override;
  void close() noexcept override;
  void poll_events() noexcept override;
  void swap_buffers() noexcept override;
  void sync_client_size_from_window() noexcept override;
  void make_current() const noexcept override;

  [[nodiscard]] void* native_window_handle() const noexcept override;
  [[nodiscard]] unsigned create_hud_bitmap_font_lists() noexcept override;
  void destroy_hud_bitmap_font_lists(unsigned base, int count) noexcept override;
  [[nodiscard]] GlWindowHotkeyEdges poll_hotkey_edges() noexcept override;

  [[nodiscard]] GraphicsApi window_graphics_api() const noexcept override { return win_api_; }

  [[nodiscard]] GlOffscreenWin32* offscreen_gl() noexcept override { return offscreen_.get(); }

 private:
  GraphicsApi win_api_ = GraphicsApi::OpenGL;
  void* hwnd_ = nullptr;
  void* hdc_ = nullptr;
  void* hglrc_ = nullptr;
  std::unique_ptr<GlOffscreenWin32> offscreen_;
};

}  // namespace cw::render
