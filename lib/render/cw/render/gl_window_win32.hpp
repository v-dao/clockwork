#pragma once

#include "cw/render/gl_window.hpp"

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

 private:
  void* hwnd_ = nullptr;
  void* hdc_ = nullptr;
  void* hglrc_ = nullptr;
};

}  // namespace cw::render
