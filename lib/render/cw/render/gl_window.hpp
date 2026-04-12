#pragma once

namespace cw::render {

/// Win32 菜单项选中时回调（`cmd_id` 为 `WM_COMMAND` 的 `LOWORD(wParam)`）。
using MenuCommandFn = void (*)(unsigned cmd_id, void* user_data);

/// 最小 OpenGL 窗口配置（阶段 4：当前仅 Windows Win32+WGL 实现）。
struct GlWindowConfig {
  int width = 1024;
  int height = 768;
  const char* title_utf8 = "Clockwork";
};

class GlWindow {
 public:
  GlWindow() = default;
  ~GlWindow();

  GlWindow(const GlWindow&) = delete;
  GlWindow& operator=(const GlWindow&) = delete;

  /// 创建窗口与 OpenGL 上下文。非 Windows 平台返回 false。
  [[nodiscard]] bool open(const GlWindowConfig& cfg);

  void close() noexcept;

  [[nodiscard]] bool is_open() const noexcept { return open_; }

  /// 处理消息队列；若用户关闭窗口则之后 `should_close()` 为 true。
  void poll_events() noexcept;

  [[nodiscard]] bool should_close() const noexcept { return should_close_; }

  [[nodiscard]] int client_width() const noexcept { return client_w_; }
  [[nodiscard]] int client_height() const noexcept { return client_h_; }

  /// 从 OS 重新读取客户区（SetMenu 等会缩小客户区，未必触发 WM_SIZE；附加菜单后应调用）。
  void sync_client_size_from_window() noexcept;

  /// 客户区像素坐标（左上为原点，y 向下），由 WM_MOUSEMOVE / 按下时更新。
  [[nodiscard]] int mouse_client_x() const noexcept { return mouse_x_; }
  [[nodiscard]] int mouse_client_y() const noexcept { return mouse_y_; }
  [[nodiscard]] bool left_button_down() const noexcept { return left_down_; }

  void swap_buffers() noexcept;

  /// Win32 窗口过程回调使用（阶段 4）；其它平台无操作。
  void win32_notify_close_request() noexcept { should_close_ = true; }
  void win32_notify_client_size(int w, int h) noexcept;
  void win32_set_mouse_client(int x, int y) noexcept;
  void win32_set_left_button(bool down) noexcept;

  /// 累加 WM_MOUSEWHEEL 增量（通常为 ±120 的倍数）；`consume_wheel_delta` 读出并清零。
  void win32_add_wheel_delta(int delta) noexcept { wheel_delta_accum_ += delta; }
  [[nodiscard]] int consume_wheel_delta() noexcept;

#ifdef _WIN32
  [[nodiscard]] void* win32_hwnd() const noexcept { return hwnd_; }
  [[nodiscard]] void* win32_hdc() const noexcept { return hdc_; }
  /// 确保后续 OpenGL 调用使用本窗口的 RC（消息循环后可能需重新绑定）。
  void make_current() const noexcept;
#endif
  /// 注册菜单 `WM_COMMAND` 回调；仅 Win32 窗口过程会触发。
  void set_menu_command_callback(MenuCommandFn fn, void* user_data) noexcept {
    menu_cb_ = fn;
    menu_user_ = user_data;
  }
  /// 供窗口过程转发菜单命令（内部调用 `menu_cb_`）。
  void notify_menu_command(unsigned cmd_id) noexcept {
    if (menu_cb_ != nullptr) {
      menu_cb_(cmd_id, menu_user_);
    }
  }

 private:
#ifdef _WIN32
  void* hwnd_ = nullptr;
  void* hdc_ = nullptr;
  void* hglrc_ = nullptr;
#endif
  MenuCommandFn menu_cb_ = nullptr;
  void* menu_user_ = nullptr;
  bool open_ = false;
  bool should_close_ = false;
  int client_w_ = 0;
  int client_h_ = 0;
  int wheel_delta_accum_ = 0;
  int mouse_x_ = 0;
  int mouse_y_ = 0;
  bool left_down_ = false;
};

inline void GlWindow::win32_notify_client_size(int w, int h) noexcept {
  client_w_ = w < 1 ? 1 : w;
  client_h_ = h < 1 ? 1 : h;
}

inline int GlWindow::consume_wheel_delta() noexcept {
  const int t = wheel_delta_accum_;
  wheel_delta_accum_ = 0;
  return t;
}

inline void GlWindow::win32_set_mouse_client(int x, int y) noexcept {
  mouse_x_ = x;
  mouse_y_ = y;
}

inline void GlWindow::win32_set_left_button(bool down) noexcept { left_down_ = down; }

}  // namespace cw::render
