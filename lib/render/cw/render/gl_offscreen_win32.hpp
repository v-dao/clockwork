#pragma once

#ifdef _WIN32

namespace cw::render {

/// 独立隐藏窗口 + WGL 3.3 兼容上下文 + FBO，用于在 Vulkan 主窗口上复用现有 OpenGL 场景绘制。
class GlOffscreenWin32 {
 public:
  GlOffscreenWin32() = default;
  ~GlOffscreenWin32();

  GlOffscreenWin32(const GlOffscreenWin32&) = delete;
  GlOffscreenWin32& operator=(const GlOffscreenWin32&) = delete;

  [[nodiscard]] bool initialize() noexcept;
  void shutdown() noexcept;

  void make_current() const noexcept;

  /// 调整颜色 + 深度附件；成功后可 `bind_draw_framebuffer()` 并 `glViewport(0,0,w,h)`。
  [[nodiscard]] bool ensure_framebuffer(int width, int height) noexcept;
  void bind_draw_framebuffer() noexcept;

  [[nodiscard]] unsigned create_hud_bitmap_font_lists() noexcept;
  void destroy_hud_bitmap_font_lists(unsigned base, int count) noexcept;

  /// 自当前绑定的绘制 FBO（颜色附件 0）读取 BGRA8，行紧密排列 `width*4` 字节。
  void read_pixels_bgra_tight(int width, int height, unsigned char* dst) noexcept;

 private:
  void destroy_gl_framebuffer() noexcept;
  void load_gl_entry_points() noexcept;

  void* hwnd_ = nullptr;
  void* hdc_ = nullptr;
  void* hglrc_ = nullptr;

  unsigned fbo_ = 0;
  unsigned color_tex_ = 0;
  unsigned depth_rbo_ = 0;
  int fb_w_ = 0;
  int fb_h_ = 0;

  bool gl_loaded_ = false;
};

}  // namespace cw::render

#endif  // _WIN32
