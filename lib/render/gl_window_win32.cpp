#include "cw/render/gl_window.hpp"

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

#include <cstdint>
#include <string>
#include <vector>

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

GlWindow* from_hwnd(HWND hwnd, UINT msg, LPARAM lParam) {
  if (msg == WM_NCCREATE) {
    const auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
    auto* self = static_cast<GlWindow*>(cs->lpCreateParams);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    return self;
  }
  return reinterpret_cast<GlWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK GlWindowWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  GlWindow* self = from_hwnd(hwnd, msg, lParam);
  if (self == nullptr) {
    return DefWindowProcW(hwnd, msg, wParam, lParam);
  }
  switch (msg) {
    case WM_CLOSE:
      self->win32_notify_close_request();
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    case WM_SIZE: {
      int cw = LOWORD(lParam);
      int ch = HIWORD(lParam);
      self->win32_notify_client_size(cw, ch);
      return 0;
    }
       case WM_MOUSEWHEEL: {
      self->win32_add_wheel_delta(static_cast<int>(GET_WHEEL_DELTA_WPARAM(wParam)));
      return 0;
    }
    case WM_LBUTTONDOWN: {
      self->win32_set_mouse_client(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
      self->win32_set_left_button(true);
      SetCapture(hwnd);
      return 0;
    }
    case WM_LBUTTONUP: {
      self->win32_set_mouse_client(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
      self->win32_set_left_button(false);
      if (GetCapture() == hwnd) {
        ReleaseCapture();
      }
      return 0;
    }
    case WM_MOUSEMOVE: {
      self->win32_set_mouse_client(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
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

/// 在创建任何 HWND 之前调用。未声明 DPI 感知时，系统会按 ~96 DPI 绘制再整体放大，菜单栏/标题栏易模糊。
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
      /// `DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2` = (HANDLE)-4（Windows 10 1703+）
      if (set_ctx(reinterpret_cast<void*>(static_cast<ULONG_PTR>(-4))) != FALSE) {
        return;
      }
    }
  }
  SetProcessDPIAware();
}

}  // namespace

GlWindow::~GlWindow() { close(); }

bool GlWindow::open(const GlWindowConfig& cfg) {
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
  client_w_ = cr.right - cr.left;
  client_h_ = cr.bottom - cr.top;
  if (client_w_ < 1) {
    client_w_ = 1;
  }
  if (client_h_ < 1) {
    client_h_ = 1;
  }

  return true;
}

void GlWindow::close() noexcept {
  if (!open_) {
    return;
  }
  HGLRC hglrc = static_cast<HGLRC>(hglrc_);
  HDC hdc = static_cast<HDC>(hdc_);
  HWND hwnd = static_cast<HWND>(hwnd_);

  wglMakeCurrent(nullptr, nullptr);
  if (hglrc != nullptr) {
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

void GlWindow::poll_events() noexcept {
  MSG msg{};
  while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE) != 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
}

void GlWindow::swap_buffers() noexcept {
  if (hdc_ != nullptr) {
    SwapBuffers(static_cast<HDC>(hdc_));
  }
}

void GlWindow::make_current() const noexcept {
  if (hdc_ != nullptr && hglrc_ != nullptr) {
    wglMakeCurrent(static_cast<HDC>(hdc_), static_cast<HGLRC>(hglrc_));
  }
}

void GlWindow::sync_client_size_from_window() noexcept {
  if (!open_ || hwnd_ == nullptr) {
    return;
  }
  RECT cr{};
  GetClientRect(static_cast<HWND>(hwnd_), &cr);
  win32_notify_client_size(cr.right - cr.left, cr.bottom - cr.top);
}

}  // namespace cw::render

#else  // !_WIN32

namespace cw::render {

GlWindow::~GlWindow() = default;

bool GlWindow::open(const GlWindowConfig&) { return false; }

void GlWindow::close() noexcept {}

void GlWindow::poll_events() noexcept {}

void GlWindow::swap_buffers() noexcept {}

void GlWindow::sync_client_size_from_window() noexcept {}

}  // namespace cw::render

#endif
