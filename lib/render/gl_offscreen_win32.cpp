#include "cw/render/gl_offscreen_win32.hpp"

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

#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif
#ifndef GL_FRAMEBUFFER
#define GL_FRAMEBUFFER 0x8D40
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_COLOR_ATTACHMENT0
#define GL_COLOR_ATTACHMENT0 0x8CE0
#endif
#ifndef GL_DEPTH_ATTACHMENT
#define GL_DEPTH_ATTACHMENT 0x8D00
#endif
#ifndef GL_FRAMEBUFFER_COMPLETE
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#endif
#ifndef GL_RGBA8
#define GL_RGBA8 0x8058
#endif
#ifndef GL_DEPTH_COMPONENT24
#define GL_DEPTH_COMPONENT24 0x81A6
#endif

#include <cstdint>

namespace cw::render {

namespace {

typedef HGLRC(WINAPI* PFN_wglCreateContextAttribsARB)(HDC, HGLRC, const int*);

using GlBindFramebuffer = void(APIENTRY*)(GLenum, GLuint);
using GlDeleteFramebuffers = void(APIENTRY*)(GLsizei, const GLuint*);
using GlGenFramebuffers = void(APIENTRY*)(GLsizei, GLuint*);
using GlCheckFramebufferStatus = GLenum(APIENTRY*)(GLenum);
using GlFramebufferTexture2D = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using GlFramebufferRenderbuffer = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint);
using GlBindRenderbuffer = void(APIENTRY*)(GLenum, GLuint);
using GlDeleteRenderbuffers = void(APIENTRY*)(GLsizei, const GLuint*);
using GlGenRenderbuffers = void(APIENTRY*)(GLsizei, GLuint*);
using GlRenderbufferStorage = void(APIENTRY*)(GLenum, GLenum, GLsizei, GLsizei);
using GlReadBuffer = void(APIENTRY*)(GLenum);

GlBindFramebuffer g_glBindFramebuffer = nullptr;
GlDeleteFramebuffers g_glDeleteFramebuffers = nullptr;
GlGenFramebuffers g_glGenFramebuffers = nullptr;
GlCheckFramebufferStatus g_glCheckFramebufferStatus = nullptr;
GlFramebufferTexture2D g_glFramebufferTexture2D = nullptr;
GlFramebufferRenderbuffer g_glFramebufferRenderbuffer = nullptr;
GlBindRenderbuffer g_glBindRenderbuffer = nullptr;
GlDeleteRenderbuffers g_glDeleteRenderbuffers = nullptr;
GlGenRenderbuffers g_glGenRenderbuffers = nullptr;
GlRenderbufferStorage g_glRenderbufferStorage = nullptr;
GlReadBuffer g_glReadBuffer = nullptr;

constexpr wchar_t kClassName[] = L"ClockworkGlOffscreen";

LRESULT CALLBACK OffscreenWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
  return DefWindowProcW(hwnd, msg, wParam, lParam);
}

}  // namespace

void GlOffscreenWin32::load_gl_entry_points() noexcept {
  if (gl_loaded_) {
    return;
  }
  gl_loaded_ = true;
  g_glBindFramebuffer = reinterpret_cast<GlBindFramebuffer>(wglGetProcAddress("glBindFramebuffer"));
  g_glDeleteFramebuffers = reinterpret_cast<GlDeleteFramebuffers>(wglGetProcAddress("glDeleteFramebuffers"));
  g_glGenFramebuffers = reinterpret_cast<GlGenFramebuffers>(wglGetProcAddress("glGenFramebuffers"));
  g_glCheckFramebufferStatus =
      reinterpret_cast<GlCheckFramebufferStatus>(wglGetProcAddress("glCheckFramebufferStatus"));
  g_glFramebufferTexture2D =
      reinterpret_cast<GlFramebufferTexture2D>(wglGetProcAddress("glFramebufferTexture2D"));
  g_glFramebufferRenderbuffer =
      reinterpret_cast<GlFramebufferRenderbuffer>(wglGetProcAddress("glFramebufferRenderbuffer"));
  g_glBindRenderbuffer = reinterpret_cast<GlBindRenderbuffer>(wglGetProcAddress("glBindRenderbuffer"));
  g_glDeleteRenderbuffers = reinterpret_cast<GlDeleteRenderbuffers>(wglGetProcAddress("glDeleteRenderbuffers"));
  g_glGenRenderbuffers = reinterpret_cast<GlGenRenderbuffers>(wglGetProcAddress("glGenRenderbuffers"));
  g_glRenderbufferStorage = reinterpret_cast<GlRenderbufferStorage>(wglGetProcAddress("glRenderbufferStorage"));
  g_glReadBuffer = reinterpret_cast<GlReadBuffer>(wglGetProcAddress("glReadBuffer"));
}

GlOffscreenWin32::~GlOffscreenWin32() { shutdown(); }

void GlOffscreenWin32::shutdown() noexcept {
  destroy_gl_framebuffer();
  if (hglrc_ != nullptr) {
    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(static_cast<HGLRC>(hglrc_));
    hglrc_ = nullptr;
  }
  if (hwnd_ != nullptr && hdc_ != nullptr) {
    ReleaseDC(static_cast<HWND>(hwnd_), static_cast<HDC>(hdc_));
  }
  hdc_ = nullptr;
  if (hwnd_ != nullptr) {
    DestroyWindow(static_cast<HWND>(hwnd_));
    hwnd_ = nullptr;
  }
  gl_loaded_ = false;
}

void GlOffscreenWin32::destroy_gl_framebuffer() noexcept {
  if (hglrc_ == nullptr) {
    fbo_ = color_tex_ = depth_rbo_ = 0;
    fb_w_ = fb_h_ = 0;
    return;
  }
  wglMakeCurrent(static_cast<HDC>(hdc_), static_cast<HGLRC>(hglrc_));
  if (fbo_ != 0U && g_glDeleteFramebuffers != nullptr) {
    g_glDeleteFramebuffers(1, &fbo_);
  }
  if (color_tex_ != 0U) {
    glDeleteTextures(1, &color_tex_);
  }
  if (depth_rbo_ != 0U && g_glDeleteRenderbuffers != nullptr) {
    g_glDeleteRenderbuffers(1, &depth_rbo_);
  }
  fbo_ = color_tex_ = depth_rbo_ = 0;
  fb_w_ = fb_h_ = 0;
}

bool GlOffscreenWin32::initialize() noexcept {
  if (hwnd_ != nullptr) {
    return true;
  }

  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = OffscreenWndProc;
  wc.hInstance = inst;
  wc.lpszClassName = kClassName;
  if (RegisterClassW(&wc) == 0) {
    if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
      return false;
    }
  }

  HWND hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kClassName, L"", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, inst,
                              nullptr);
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
  load_gl_entry_points();
  if (g_glGenFramebuffers == nullptr || g_glGenRenderbuffers == nullptr) {
    shutdown();
    return false;
  }
  return true;
}

void GlOffscreenWin32::make_current() const noexcept {
  if (hdc_ != nullptr && hglrc_ != nullptr) {
    wglMakeCurrent(static_cast<HDC>(hdc_), static_cast<HGLRC>(hglrc_));
  }
}

bool GlOffscreenWin32::ensure_framebuffer(int width, int height) noexcept {
  if (width < 1 || height < 1 || hglrc_ == nullptr) {
    return false;
  }
  if (width == fb_w_ && height == fb_h_ && fbo_ != 0U) {
    return true;
  }
  destroy_gl_framebuffer();
  make_current();

  g_glGenFramebuffers(1, &fbo_);
  glGenTextures(1, &color_tex_);
  g_glGenRenderbuffers(1, &depth_rbo_);
  if (fbo_ == 0U || color_tex_ == 0U || depth_rbo_ == 0U) {
    destroy_gl_framebuffer();
    return false;
  }

  glBindTexture(GL_TEXTURE_2D, color_tex_);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

  g_glBindRenderbuffer(GL_RENDERBUFFER, depth_rbo_);
  g_glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
  g_glBindRenderbuffer(GL_RENDERBUFFER, 0);

  g_glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  g_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color_tex_, 0);
  g_glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rbo_);

  const GLenum st = g_glCheckFramebufferStatus(GL_FRAMEBUFFER);
  g_glBindFramebuffer(GL_FRAMEBUFFER, 0);
  if (st != GL_FRAMEBUFFER_COMPLETE) {
    destroy_gl_framebuffer();
    return false;
  }

  fb_w_ = width;
  fb_h_ = height;
  return true;
}

void GlOffscreenWin32::bind_draw_framebuffer() noexcept {
  if (fbo_ != 0U && g_glBindFramebuffer != nullptr) {
    g_glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
  }
}

unsigned GlOffscreenWin32::create_hud_bitmap_font_lists() noexcept {
  HDC hdc = static_cast<HDC>(hdc_);
  if (hdc == nullptr || hglrc_ == nullptr) {
    return 0;
  }
  make_current();
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

void GlOffscreenWin32::destroy_hud_bitmap_font_lists(unsigned base, int count) noexcept {
  if (base != 0U && count > 0 && hglrc_ != nullptr) {
    make_current();
    glDeleteLists(static_cast<GLuint>(base), count);
  }
}

void GlOffscreenWin32::read_pixels_bgra_tight(int width, int height, unsigned char* dst) noexcept {
  if (dst == nullptr || width < 1 || height < 1) {
    return;
  }
  make_current();
  if (g_glReadBuffer != nullptr) {
    g_glReadBuffer(GL_COLOR_ATTACHMENT0);
  }
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, dst);
}

}  // namespace cw::render

#endif  // _WIN32
