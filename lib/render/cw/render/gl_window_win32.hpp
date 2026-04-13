#pragma once

#include "cw/render/gl_offscreen_win32.hpp"
#include "cw/render/gl_window.hpp"

#include <memory>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace cw::render {

class GlWindowWin32 final : public GlWindow {
#ifdef _WIN32
  friend LRESULT CALLBACK GlWindowFrameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
  friend LRESULT CALLBACK GlWindowClientWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
#endif
 public:
  ~GlWindowWin32() override;

  [[nodiscard]] bool open(const GlWindowConfig& cfg) override;
  void close() noexcept override;
  void poll_events() noexcept override;
  void swap_buffers() noexcept override;
  void sync_client_size_from_window() noexcept override;
  void make_current() const noexcept override;

  [[nodiscard]] void* native_window_handle() const noexcept override;
  [[nodiscard]] void* native_menu_host_handle() const noexcept override;
  [[nodiscard]] bool try_set_window_graphics_api(GraphicsApi api) noexcept override;
  [[nodiscard]] unsigned create_hud_bitmap_font_lists() noexcept override;
  void destroy_hud_bitmap_font_lists(unsigned base, int count) noexcept override;
  [[nodiscard]] GlWindowHotkeyEdges poll_hotkey_edges() noexcept override;

  [[nodiscard]] GraphicsApi window_graphics_api() const noexcept override { return win_api_; }

  [[nodiscard]] GlOffscreenWin32* offscreen_gl() noexcept override { return offscreen_.get(); }

 private:
  [[nodiscard]] bool create_or_resize_client_child() noexcept;
  [[nodiscard]] bool init_vulkan_client_branch() noexcept;
  [[nodiscard]] bool init_opengl_client_branch() noexcept;
  void destroy_client_gl_context() noexcept;
  void destroy_client_window_only() noexcept;
  [[nodiscard]] bool restore_win_api_after_failed_switch(GraphicsApi previous) noexcept;

  GraphicsApi win_api_ = GraphicsApi::OpenGL;
  void* hwnd_frame_ = nullptr;
  void* hwnd_client_ = nullptr;
  void* hdc_ = nullptr;
  void* hglrc_ = nullptr;
  std::unique_ptr<GlOffscreenWin32> offscreen_;
};

}  // namespace cw::render
