#include "cw/render/gl_window_win32.hpp"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include <windows.h>

#include <GL/gl.h>

#ifndef WGL_CONTEXT_MAJOR_VERSION_ARB
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002
#endif

typedef HGLRC(WINAPI* PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);

#ifndef GET_WHEEL_DELTA_WPARAM
#define GET_WHEEL_DELTA_WPARAM(wParam) (static_cast<std::int16_t>(HIWORD(wParam)))
#endif
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) (static_cast<int>(static_cast<std::int16_t>(LOWORD(lp))))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) (static_cast<int>(static_cast<std::int16_t>(HIWORD(lp))))
#endif

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "cw/render/graphics_types.hpp"

namespace cw::render {

namespace {

std::wstring utf8_to_wide(const char* utf8) {
  if (utf8 == nullptr || utf8[0] == '\0') {
    return L"Clockwork";
  }
  const int n = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
  if (n <= 0) {
    return L"Clockwork";
  }
  std::vector<wchar_t> buf(static_cast<std::size_t>(n));
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, buf.data(), n);
  return std::wstring(buf.data());
}

GlWindowWin32* from_hwnd(HWND hwnd, UINT msg, LPARAM lParam) {
  if (msg == WM_NCCREATE) {
    const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    auto* self = static_cast<GlWindowWin32*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    return self;
  }
  return reinterpret_cast<GlWindowWin32*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

constexpr wchar_t kFrameClassName[] = L"ClockworkGlFrame";
constexpr wchar_t kClientClassName[] = L"ClockworkGlClient";

void ensure_process_dpi_aware() {
  static bool done = false;
  if (done) {
    return;
  }
  done = true;
  HMODULE user = GetModuleHandleW(L"user32.dll");
  if (user != nullptr) {
    using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(void*);
    void* proc = reinterpret_cast<void*>(GetProcAddress(user, "SetProcessDpiAwarenessContext"));
    auto set_ctx = reinterpret_cast<SetProcessDpiAwarenessContextFn>(proc);
    if (set_ctx != nullptr) {
      if (set_ctx(reinterpret_cast<void*>(static_cast<ULONG_PTR>(-4))) != FALSE) {
        return;
      }
    }
  }
  SetProcessDPIAware();
}

}  // namespace

LRESULT CALLBACK GlWindowFrameWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  GlWindowWin32* self = from_hwnd(hwnd, msg, lParam);
  if (self == nullptr) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
  switch (msg) {
    case WM_CLOSE:
      self->platform_notify_close_request();
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_SIZE: {
      const int cw = std::max(1, static_cast<int>(LOWORD(lParam)));
      const int ch = std::max(1, static_cast<int>(HIWORD(lParam)));
      if (self->hwnd_client_ != nullptr) {
        SetWindowPos(static_cast<HWND>(self->hwnd_client_), nullptr, 0, 0, cw, ch, SWP_NOZORDER | SWP_NOACTIVATE);
      }
      self->platform_notify_client_size(cw, ch);
      return 0;
    }
    case WM_COMMAND: {
      self->notify_menu_command(static_cast<unsigned>(LOWORD(wParam)));
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK GlWindowClientWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  GlWindowWin32* self = from_hwnd(hwnd, msg, lParam);
  if (self == nullptr) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
  switch (msg) {
    case WM_MOUSEWHEEL: {
      self->add_wheel_delta(static_cast<int>(GET_WHEEL_DELTA_WPARAM(wParam)));
      return 0;
    }
    case WM_LBUTTONDOWN: {
      self->platform_set_mouse_client(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
      self->platform_set_left_button(true);
      SetCapture(hwnd);
      return 0;
    }
    case WM_LBUTTONUP: {
      self->platform_set_mouse_client(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
      self->platform_set_left_button(false);
      if (GetCapture() == hwnd) {
        ReleaseCapture();
      }
      return 0;
    }
    case WM_MOUSEMOVE: {
      self->platform_set_mouse_client(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

GlWindowWin32::~GlWindowWin32() { close(); }

bool GlWindowWin32::create_or_resize_client_child() noexcept {
  HWND frame = static_cast<HWND>(hwnd_frame_);
  if (frame == nullptr) {
    return false;
  }
  RECT cr{};
  if (GetClientRect(frame, &cr) == 0) {
    return false;
  }
  int cw = std::max(1, static_cast<int>(cr.right - cr.left));
  int ch = std::max(1, static_cast<int>(cr.bottom - cr.top));

  if (hwnd_client_ != nullptr) {
    destroy_client_gl_context();
    destroy_client_window_only();
  }

  HINSTANCE inst = GetModuleHandleW(nullptr);
  HWND client = CreateWindowExW(0, kClientClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, cw, ch, frame, nullptr, inst,
                                this);
  if (client == nullptr) {
    return false;
  }
  hwnd_client_ = client;
  return true;
}

void GlWindowWin32::destroy_client_gl_context() noexcept {
  HGLRC hglrc = static_cast<HGLRC>(hglrc_);
  HWND client = static_cast<HWND>(hwnd_client_);
  HDC hdc = static_cast<HDC>(hdc_);
  if (hglrc != nullptr) {
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(hglrc);
    hglrc_ = nullptr;
  }
  if (client != nullptr && hdc != nullptr) {
    ReleaseDC(client, hdc);
  }
  hdc_ = nullptr;
}

void GlWindowWin32::destroy_client_window_only() noexcept {
  if (hwnd_client_ != nullptr) {
    DestroyWindow(static_cast<HWND>(hwnd_client_));
    hwnd_client_ = nullptr;
  }
}

bool GlWindowWin32::restore_win_api_after_failed_switch(GraphicsApi previous) noexcept {
  offscreen_.reset();
  destroy_client_gl_context();
  destroy_client_window_only();
  win_api_ = previous;
  if (!create_or_resize_client_child()) {
    return false;
  }
  if (previous == GraphicsApi::Vulkan) {
    return init_vulkan_client_branch();
  }
  return init_opengl_client_branch();
}

bool GlWindowWin32::init_opengl_client_branch() noexcept {
  HWND client = static_cast<HWND>(hwnd_client_);
  if (client == nullptr) {
    return false;
  }
  HDC hdc = GetDC(client);
  if (hdc == nullptr) {
    return false;
  }

  PIXELFORMATDESCRIPTOR pfd{};
  pfd.nSize = sizeof(pfd);
  pfd.nVersion = 1;
  pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  pfd.iPixelType = PFD_TYPE_RGBA;
  pfd.cColorBits = 32;
  pfd.cDepthBits = 24;
  pfd.iLayerType = PFD_MAIN_PLANE;

  const int pf = ChoosePixelFormat(hdc, &pfd);
  if (pf == 0 || SetPixelFormat(hdc, pf, &pfd) == FALSE) {
    ReleaseDC(client, hdc);
    return false;
  }

  HGLRC hglrc = nullptr;
  {
    HGLRC temp = wglCreateContext(hdc);
    if (temp == nullptr) {
      ReleaseDC(client, hdc);
      return false;
    }
    if (wglMakeCurrent(hdc, temp) == FALSE) {
      wglDeleteContext(temp);
      ReleaseDC(client, hdc);
      return false;
    }
    auto create_attribs = reinterpret_cast<PFN_wglCreateContextAttribsARB>(
        reinterpret_cast<void*>(wglGetProcAddress("wglCreateContextAttribsARB")));
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(temp);

    if (create_attribs != nullptr) {
      const int attribs[] = {WGL_CONTEXT_MAJOR_VERSION_ARB,
                             3,
                             WGL_CONTEXT_MINOR_VERSION_ARB,
                             3,
                             WGL_CONTEXT_PROFILE_MASK_ARB,
                             WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
                             0,
                             0};
      hglrc = create_attribs(hdc, nullptr, attribs);
    }
    if (hglrc == nullptr) {
      hglrc = wglCreateContext(hdc);
    }
    if (hglrc == nullptr) {
      ReleaseDC(client, hdc);
      return false;
    }
    if (wglMakeCurrent(hdc, hglrc) == FALSE) {
      wglDeleteContext(hglrc);
      ReleaseDC(client, hdc);
      return false;
    }
  }

  hdc_ = hdc;
  hglrc_ = hglrc;
  return true;
}

bool GlWindowWin32::init_vulkan_client_branch() noexcept {
  hdc_ = nullptr;
  hglrc_ = nullptr;
  offscreen_ = std::make_unique<GlOffscreenWin32>();
  if (!offscreen_->initialize()) {
    offscreen_.reset();
    return false;
  }
  return true;
}

bool GlWindowWin32::open(const GlWindowConfig& cfg) {
  ensure_process_dpi_aware();

  if (open_) {
    close();
  }
  should_close_ = false;

  HINSTANCE inst = GetModuleHandleW(nullptr);

  WNDCLASSW wc_frame{};
  wc_frame.lpfnWndProc = GlWindowFrameWndProc;
  wc_frame.hInstance = inst;
  wc_frame.hCursor = LoadCursorA(nullptr, IDC_ARROW);
  wc_frame.hbrBackground = nullptr;
  wc_frame.lpszMenuName = nullptr;
  wc_frame.lpszClassName = kFrameClassName;
  if (RegisterClassW(&wc_frame) == 0) {
    if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      return false;
    }
  }

  WNDCLASSW wc_client{};
  wc_client.style = CS_OWNDC;
  wc_client.lpfnWndProc = GlWindowClientWndProc;
  wc_client.hInstance = inst;
  wc_client.hCursor = LoadCursorA(nullptr, IDC_ARROW);
  wc_client.hbrBackground = nullptr;
  wc_client.lpszMenuName = nullptr;
  wc_client.lpszClassName = kClientClassName;
  if (RegisterClassW(&wc_client) == 0) {
    if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      return false;
    }
  }

  const std::wstring title = utf8_to_wide(cfg.title_utf8);

  RECT r{0, 0, cfg.width, cfg.height};
  constexpr DWORD style = WS_OVERLAPPEDWINDOW;
  AdjustWindowRect(&r, style, FALSE);

  HWND frame = CreateWindowExW(0, kFrameClassName, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                               r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, this);
  if (frame == nullptr) {
    return false;
  }
  hwnd_frame_ = frame;

  win_api_ = cfg.window_graphics_api;

  if (!create_or_resize_client_child()) {
    DestroyWindow(frame);
    hwnd_frame_ = nullptr;
    return false;
  }

  if (win_api_ == GraphicsApi::Vulkan) {
    if (!init_vulkan_client_branch()) {
      offscreen_.reset();
      destroy_client_window_only();
      DestroyWindow(frame);
      hwnd_frame_ = nullptr;
      return false;
    }
  } else {
    if (!init_opengl_client_branch()) {
      destroy_client_window_only();
      DestroyWindow(frame);
      hwnd_frame_ = nullptr;
      return false;
    }
  }

  open_ = true;
  ShowWindow(frame, SW_SHOWMAXIMIZED);
  UpdateWindow(frame);
  sync_client_size_from_window();
  return true;
}

void GlWindowWin32::close() noexcept {
  if (!open_) {
    return;
  }
  offscreen_.reset();
  destroy_client_gl_context();
  destroy_client_window_only();

  HWND frame = static_cast<HWND>(hwnd_frame_);
  if (frame != nullptr) {
    DestroyWindow(frame);
  }
  hwnd_frame_ = nullptr;
  open_ = false;
}

void GlWindowWin32::poll_events() noexcept {
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

void GlWindowWin32::swap_buffers() noexcept {
  if (hdc_ != nullptr) {
    SwapBuffers(static_cast<HDC>(hdc_));
  }
}

void GlWindowWin32::make_current() const noexcept {
  if (hdc_ != nullptr && hglrc_ != nullptr) {
    wglMakeCurrent(static_cast<HDC>(hdc_), static_cast<HGLRC>(hglrc_));
  }
}

void GlWindowWin32::sync_client_size_from_window() noexcept {
  if (!open_ || hwnd_frame_ == nullptr) {
    return;
  }
  RECT cr{};
  GetClientRect(static_cast<HWND>(hwnd_frame_), &cr);
  platform_notify_client_size(cr.right - cr.left, cr.bottom - cr.top);
}

void* GlWindowWin32::native_window_handle() const noexcept { return hwnd_client_; }

void* GlWindowWin32::native_menu_host_handle() const noexcept { return hwnd_frame_; }

bool GlWindowWin32::try_set_window_graphics_api(GraphicsApi api) noexcept {
  if (!open_ || hwnd_frame_ == nullptr) {
    return false;
  }
  if (api == win_api_) {
    return true;
  }

  const GraphicsApi previous = win_api_;

  offscreen_.reset();
  destroy_client_gl_context();

  if (hwnd_client_ != nullptr) {
    DestroyWindow(static_cast<HWND>(hwnd_client_));
    hwnd_client_ = nullptr;
  }

  win_api_ = api;

  if (!create_or_resize_client_child()) {
    static_cast<void>(restore_win_api_after_failed_switch(previous));
    return false;
  }

  if (win_api_ == GraphicsApi::Vulkan) {
    if (!init_vulkan_client_branch()) {
      offscreen_.reset();
      destroy_client_window_only();
      static_cast<void>(restore_win_api_after_failed_switch(previous));
      return false;
    }
  } else {
    if (!init_opengl_client_branch()) {
      destroy_client_window_only();
      static_cast<void>(restore_win_api_after_failed_switch(previous));
      return false;
    }
  }

  sync_client_size_from_window();
  return true;
}

unsigned GlWindowWin32::create_hud_bitmap_font_lists() noexcept {
  if (win_api_ == GraphicsApi::Vulkan && offscreen_ != nullptr) {
    return offscreen_->create_hud_bitmap_font_lists();
  }
  HDC hdc = static_cast<HDC>(hdc_);
  if (hdc == nullptr || hglrc_ == nullptr) {
    return 0;
  }
  const GLuint base = glGenLists(96);
  if (base == 0) {
    return 0;
  }
  const int log_pixels_y = GetDeviceCaps(hdc, LOGPIXELSY);
  const int font_height = -MulDiv(12, log_pixels_y, 72);
  HFONT hf = CreateFontW(font_height, 0, 0, 0, FW_MEDIUM, 0, 0, 0, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
                         CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, nullptr);
  if (hf == nullptr) {
    glDeleteLists(base, 96);
    return 0;
  }
  HFONT old = static_cast<HFONT>(SelectObject(hdc, hf));
  if (wglUseFontBitmapsW(hdc, 32, 96, base) == FALSE) {
    SelectObject(hdc, old);
    DeleteObject(hf);
    glDeleteLists(base, 96);
    return 0;
  }
  SelectObject(hdc, old);
  DeleteObject(hf);
  return static_cast<unsigned>(base);
}

void GlWindowWin32::destroy_hud_bitmap_font_lists(unsigned base, int count) noexcept {
  if (base == 0U || count <= 0) {
    return;
  }
  if (win_api_ == GraphicsApi::Vulkan && offscreen_ != nullptr) {
    offscreen_->destroy_hud_bitmap_font_lists(base, count);
    return;
  }
  if (hglrc_ != nullptr) {
    glDeleteLists(static_cast<GLuint>(base), count);
  }
}

GlWindowHotkeyEdges GlWindowWin32::poll_hotkey_edges() noexcept {
  GlWindowHotkeyEdges e{};
  if ((GetAsyncKeyState(VK_ESCAPE) & 0x1) != 0) {
    e.escape = true;
  }
  if ((GetAsyncKeyState(VK_HOME) & 0x1) != 0) {
    e.home_reset_view = true;
  }
  if ((GetAsyncKeyState(VK_SPACE) & 0x1) != 0) {
    e.toggle_pause = true;
  }
  if (((GetAsyncKeyState(VK_OEM_PLUS) & 0x1) != 0) || ((GetAsyncKeyState(VK_ADD) & 0x1) != 0)) {
    e.time_scale_up = true;
  }
  if (((GetAsyncKeyState(VK_OEM_MINUS) & 0x1) != 0) || ((GetAsyncKeyState(VK_SUBTRACT) & 0x1) != 0)) {
    e.time_scale_down = true;
  }
  return e;
}

}  // namespace cw::render

#endif  // _WIN32
