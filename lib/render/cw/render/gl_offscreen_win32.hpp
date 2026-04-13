#pragma once

#include <cstddef>

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

  /// 自当前绑定的绘制 FBO（颜色附件 0）读取 BGRA8，行紧密排列 `width*4` 字节（同步，阻塞直至读完成）。
  void read_pixels_bgra_tight(int width, int height, unsigned char* dst) noexcept;

  /// 将上一帧 `commit_read_pixels_bgra_async` 写入 PBO 的像素拷入 `dst`（紧密 BGRA）；无待解数据或失败时返回 false。
  /// 成功时写入 `*out_w`/`*out_h` 为该块的宽高；`dst` 须至少容纳 `(*out_w)*(*out_h)*4` 字节。
  [[nodiscard]] bool try_resolve_read_pixels_bgra_tight(unsigned char* dst, int* out_w, int* out_h) noexcept;

  /// 在绘制完成后调用：`glReadPixels` 异步写入内部 PBO（与下一帧 `try_resolve` 配对，呈现落后一帧）。
  void commit_read_pixels_bgra_async(int width, int height) noexcept;

  /// 当前 GL 是否具备 PBO + fence 异步读回（否时 Vulkan 路径应退回同步 `read_pixels_bgra_tight`）。
  [[nodiscard]] bool read_async_capable() const noexcept;

 private:
  void destroy_gl_framebuffer() noexcept;
  void destroy_async_pixel_pack() noexcept;
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

  unsigned pack_pbo_[2]{0, 0};
  void* pack_fence_[2]{nullptr, nullptr};
  int pack_slot_w_[2]{0, 0};
  int pack_slot_h_[2]{0, 0};
  std::size_t pack_pbo_bytes_[2]{0, 0};
  int pack_write_i_ = 0;
};

}  // namespace cw::render

#endif  // _WIN32
