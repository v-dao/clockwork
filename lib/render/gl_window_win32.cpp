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

LRESULT CALLBACK GlWindowWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
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
      int cw = LOWORD(lParam);
      int ch = HIWORD(lParam);
      self->platform_notify_client_size(cw, ch);
      return 0;
    }
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
    case WM_COMMAND: {
      self->notify_menu_command(static_cast<unsigned>(LOWORD(wParam)));
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    default:
      break;
  }
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

constexpr wchar_t kClassName[] = L"ClockworkGlWindow";

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

GlWindowWin32::~GlWindowWin32() { close(); }

bool GlWindowWin32::open(const GlWindowConfig& cfg) {
  ensure_process_dpi_aware();

  if (open_) {
    close();
  }
  should_close_ = false;

  HINSTANCE inst = GetModuleHandleW(nullptr);

  WNDCLASSW wc{};
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = GlWindowWndProc;
  wc.cbClsExtra = 0;
  wc.cbWndExtra = 0;
  wc.hInstance = inst;
  wc.hIcon = nullptr;
  wc.hCursor = LoadCursorA(nullptr, IDC_ARROW);
  wc.hbrBackground = nullptr;
  wc.lpszMenuName = nullptr;
  wc.lpszClassName = kClassName;
  if (RegisterClassW(&wc) == 0) {
    if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      return false;
    }
  }

  const std::wstring title = utf8_to_wide(cfg.title_utf8);

  RECT r{0, 0, cfg.width, cfg.height};
  constexpr DWORD style = WS_OVERLAPPEDWINDOW;
  AdjustWindowRect(&r, style, FALSE);

  HWND hwnd = CreateWindowExW(0, kClassName, title.c_str(), style, CW_USEDEFAULT, CW_USEDEFAULT,
                              r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, this);
  if (hwnd == nullptr) {
    return false;
  }

  win_api_ = cfg.window_graphics_api;

  if (win_api_ == GraphicsApi::Vulkan) {
    hwnd_ = hwnd;
    hdc_ = nullptr;
    hglrc_ = nullptr;
    offscreen_ = std::make_unique<GlOffscreenWin32>();
    if (!offscreen_->initialize()) {
      offscreen_.reset();
      DestroyWindow(hwnd);
      return false;
    }
    open_ = true;
    ShowWindow(hwnd, SW_SHOWMAXIMIZED);
    UpdateWindow(hwnd);
    RECT cr{};
    GetClientRect(hwnd, &cr);
    platform_notify_client_size(cr.right - cr.left, cr.bottom - cr.top);
    return true;
  }

  HDC hdc = GetDC(hwnd);
  if (hdc == nullptr) {
    DestroyWindow(hwnd);
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
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    return false;
  }

  HGLRC hglrc = nullptr;
  {
    HGLRC temp = wglCreateContext(hdc);
    if (temp == nullptr) {
      ReleaseDC(hwnd, hdc);
      DestroyWindow(hwnd);
      return false;
    }
    if (wglMakeCurrent(hdc, temp) == FALSE) {
      wglDeleteContext(temp);
      ReleaseDC(hwnd, hdc);
      DestroyWindow(hwnd);
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
      ReleaseDC(hwnd, hdc);
      DestroyWindow(hwnd);
      return false;
    }
    if (wglMakeCurrent(hdc, hglrc) == FALSE) {
      wglDeleteContext(hglrc);
      ReleaseDC(hwnd, hdc);
      DestroyWindow(hwnd);
      return false;
    }
  }

  hwnd_ = hwnd;
  hdc_ = hdc;
  hglrc_ = hglrc;
  open_ = true;

  ShowWindow(hwnd, SW_SHOWMAXIMIZED);
  UpdateWindow(hwnd);

  RECT cr{};
  GetClientRect(hwnd, &cr);
  platform_notify_client_size(cr.right - cr.left, cr.bottom - cr.top);

  return true;
}

void GlWindowWin32::close() noexcept {
  if (!open_) {
    return;
  }
  offscreen_.reset();

  HGLRC hglrc = static_cast<HGLRC>(hglrc_);
  HDC hdc = static_cast<HDC>(hdc_);
  HWND hwnd = static_cast<HWND>(hwnd_);

  if (hglrc != nullptr) {
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(hglrc);
  }
  if (hwnd != nullptr && hdc != nullptr) {
    ReleaseDC(hwnd, hdc);
  }
  if (hwnd != nullptr) {
    DestroyWindow(hwnd);
  }

  hwnd_ = nullptr;
  hdc_ = nullptr;
  hglrc_ = nullptr;
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
  if (!open_ || hwnd_ == nullptr) {
    return;
  }
  RECT cr{};
  GetClientRect(static_cast<HWND>(hwnd_), &cr);
  platform_notify_client_size(cr.right - cr.left, cr.bottom - cr.top);
}

void* GlWindowWin32::native_window_handle() const noexcept { return hwnd_; }

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
