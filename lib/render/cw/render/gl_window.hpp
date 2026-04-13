#pragma once

#include "cw/render/graphics_types.hpp"

#include <memory>

namespace cw::render {

class GlOffscreenWin32;

/// Win32 菜单项选中时回调（`cmd_id` 为 `WM_COMMAND` 的 `LOWORD(wParam)`；其它平台可映射到等价事件）。
using MenuCommandFn = void (*)(unsigned cmd_id, void* user_data);

/// 窗口创建配置。`window_graphics_api` 决定是否在客户区创建 WGL 上下文（OpenGL）或仅原生面（Vulkan）。
struct GlWindowConfig {
  int width = 1024;
  int height = 768;
  const char* title_utf8 = "Clockwork";
  GraphicsApi window_graphics_api = GraphicsApi::OpenGL;
};

/// 主循环快捷键边沿（与 `GetAsyncKeyState` &0x1 语义一致：自上次查询以来是否有过一次按下）。
struct GlWindowHotkeyEdges {
  bool escape = false;
  bool home_reset_view = false;
  bool toggle_pause = false;
  bool time_scale_up = false;
  bool time_scale_down = false;
};

/// 跨平台 OpenGL 窗口抽象：Win32/WGL、Linux（GLX 等）由各平台 TU 实现。
class GlWindow {
 public:
  GlWindow() = default;
  virtual ~GlWindow();

  GlWindow(const GlWindow&) = delete;
  GlWindow& operator=(const GlWindow&) = delete;

  [[nodiscard]] virtual bool open(const GlWindowConfig& cfg) = 0;
  virtual void close() noexcept = 0;

  [[nodiscard]] virtual GraphicsApi window_graphics_api() const noexcept { return GraphicsApi::OpenGL; }

  [[nodiscard]] bool is_open() const noexcept { return open_; }

  /// 处理消息队列；若用户关闭窗口则之后 `should_close()` 为 true。
  virtual void poll_events() noexcept = 0;

  [[nodiscard]] bool should_close() const noexcept { return should_close_; }

  [[nodiscard]] int client_width() const noexcept { return client_w_; }
  [[nodiscard]] int client_height() const noexcept { return client_h_; }

  /// 从 OS 重新读取客户区（附加菜单栏等会改变客户区时调用）。
  virtual void sync_client_size_from_window() noexcept = 0;

  [[nodiscard]] int mouse_client_x() const noexcept { return mouse_x_; }
  [[nodiscard]] int mouse_client_y() const noexcept { return mouse_y_; }
  [[nodiscard]] bool left_button_down() const noexcept { return left_down_; }

  virtual void swap_buffers() noexcept = 0;

  /// 绑定当前线程的 OpenGL 上下文（展示前调用）。
  virtual void make_current() const noexcept {}

  /// 原生窗口句柄：Win32 为 `HWND`，其它平台为约定类型指针；无则 `nullptr`。
  [[nodiscard]] virtual void* native_window_handle() const noexcept { return nullptr; }

  /// Win32 + Vulkan：离屏 WGL/FBO，用于与 Vulkan 交换链合成；其它配置返回 `nullptr`。
  [[nodiscard]] virtual GlOffscreenWin32* offscreen_gl() noexcept { return nullptr; }

  /// 使用当前 GL 上下文创建位图字体显示列表（如 WGL）；失败返回 0。
  [[nodiscard]] virtual unsigned create_hud_bitmap_font_lists() noexcept { return 0; }
  virtual void destroy_hud_bitmap_font_lists(unsigned base, int count = 96) noexcept;

  /// 在 `poll_events` 之后、主循环内调用，采样平台快捷键边沿。
  [[nodiscard]] virtual GlWindowHotkeyEdges poll_hotkey_edges() noexcept;

  void set_menu_command_callback(MenuCommandFn fn, void* user_data) noexcept {
    menu_cb_ = fn;
    menu_user_ = user_data;
  }
  void notify_menu_command(unsigned cmd_id) noexcept {
    if (menu_cb_ != nullptr) {
      menu_cb_(cmd_id, menu_user_);
    }
  }

  /// 累加滚轮增量（Win32 `WM_MOUSEWHEEL`）；实现类在消息路径中调用。
  void add_wheel_delta(int delta) noexcept { wheel_delta_accum_ += delta; }
  [[nodiscard]] int consume_wheel_delta() noexcept;

  /// 以下由平台窗口过程（如 Win32 `WndProc`）调用，非应用业务代码。
  void platform_notify_close_request() noexcept { should_close_ = true; }
  void platform_notify_client_size(int w, int h) noexcept;
  void platform_set_mouse_client(int x, int y) noexcept;
  void platform_set_left_button(bool down) noexcept;

 protected:
  bool open_ = false;
  bool should_close_ = false;
  int client_w_ = 0;
  int client_h_ = 0;
  int wheel_delta_accum_ = 0;
  int mouse_x_ = 0;
  int mouse_y_ = 0;
  bool left_down_ = false;
  MenuCommandFn menu_cb_ = nullptr;
  void* menu_user_ = nullptr;
};

[[nodiscard]] std::unique_ptr<GlWindow> create_gl_window();

inline void GlWindow::platform_notify_client_size(int w, int h) noexcept {
  client_w_ = w < 1 ? 1 : w;
  client_h_ = h < 1 ? 1 : h;
}

inline void GlWindow::platform_set_mouse_client(int x, int y) noexcept {
  mouse_x_ = x;
  mouse_y_ = y;
}

inline void GlWindow::platform_set_left_button(bool down) noexcept { left_down_ = down; }

inline int GlWindow::consume_wheel_delta() noexcept {
  const int t = wheel_delta_accum_;
  wheel_delta_accum_ = 0;
  return t;
}

}  // namespace cw::render
