#include "cw/situation_view/situation_view_chrome_win32.hpp"

#include "cw/engine/engine.hpp"
#include "cw/render/gl_window.hpp"
#include "cw/render/graphics_types.hpp"
#include "cw/scenario/scenario.hpp"
#include "cw/situation_view/situation_view_shell.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>

namespace cw::situation_view {

namespace {

constexpr unsigned kMenuView2d = 0xE100;
constexpr unsigned kMenuView3d = 0xE101;
constexpr unsigned kMenuViewSplit2d3d = 0xE103;
constexpr unsigned kMenuGlobeLighting = 0xE102;
constexpr unsigned kMenuSplitScaleSync = 0xE104;
constexpr unsigned kMenuTacticalAutoBounds = 0xE105;

constexpr unsigned kMenuSimPause = 0xE200;
constexpr unsigned kMenuSimResume = 0xE201;
constexpr unsigned kMenuSimEnd = 0xE202;
constexpr unsigned kMenuSimReset = 0xE203;
constexpr unsigned kMenuSimSpeed025 = 0xE210;
constexpr unsigned kMenuSimSpeed05 = 0xE211;
constexpr unsigned kMenuSimSpeed1 = 0xE212;
constexpr unsigned kMenuSimSpeed2 = 0xE213;
constexpr unsigned kMenuSimSpeed4 = 0xE214;

constexpr unsigned kMenuGfxOpenGL = 0xE301;
constexpr unsigned kMenuGfxVulkan = 0xE302;

}  // namespace

void Win32SituationChrome::menu_thunk(unsigned cmd, void* user) {
  if (user != nullptr) {
    static_cast<Win32SituationChrome*>(user)->on_menu_command(cmd);
  }
}

void Win32SituationChrome::on_menu_command(unsigned cmd) {
  if (shell_ == nullptr) {
    return;
  }
  SituationViewShell& sh = *shell_;
  HMENU hview = static_cast<HMENU>(hmenu_view_);
  int client_w = 1280;
  int client_h = 720;
  if (hwnd_main_ != nullptr) {
    RECT rc{};
    if (GetClientRect(static_cast<HWND>(hwnd_main_), &rc) != 0) {
      client_w = std::max(1, static_cast<int>(rc.right - rc.left));
      client_h = std::max(1, static_cast<int>(rc.bottom - rc.top));
    }
  }
  const ViewMode prev_mode = sh.view_mode_;

  if (cmd == kMenuView2d) {
    sh.view_mode_ = ViewMode::Tactical2D;
    if (hview != nullptr) {
      CheckMenuRadioItem(hview, kMenuView2d, kMenuViewSplit2d3d, kMenuView2d, MF_BYCOMMAND);
    }
    if (sh.viewport_sync_engine_ != nullptr &&
        (prev_mode == ViewMode::Globe3d || prev_mode == ViewMode::Split2dGlobe)) {
      const int split_x = std::max(1, client_w / 2);
      const int right_w = std::max(1, client_w - split_x);
      const int globe_ew_w = (prev_mode == ViewMode::Split2dGlobe) ? right_w : client_w;
      sh.sync_tactical_from_globe_viewport(sh.viewport_sync_engine_->situation_presentation(), client_w, client_h,
                                           globe_ew_w);
    }
  } else if (cmd == kMenuView3d) {
    sh.view_mode_ = ViewMode::Globe3d;
    if (hview != nullptr) {
      CheckMenuRadioItem(hview, kMenuView2d, kMenuViewSplit2d3d, kMenuView3d, MF_BYCOMMAND);
    }
    if (sh.viewport_sync_engine_ != nullptr &&
        (prev_mode == ViewMode::Tactical2D || prev_mode == ViewMode::Split2dGlobe)) {
      const int split_x = std::max(1, client_w / 2);
      const int tactical_tw = (prev_mode == ViewMode::Split2dGlobe) ? split_x : client_w;
      sh.sync_globe_from_tactical_viewport(sh.viewport_sync_engine_->situation_presentation(), client_w, client_h,
                                           tactical_tw);
    }
  } else if (cmd == kMenuViewSplit2d3d) {
    sh.view_mode_ = ViewMode::Split2dGlobe;
    sh.split_initial_sync_pending_ = true;
    if (hview != nullptr) {
      CheckMenuRadioItem(hview, kMenuView2d, kMenuViewSplit2d3d, kMenuViewSplit2d3d, MF_BYCOMMAND);
    }
  } else if (cmd == kMenuGlobeLighting) {
    sh.globe_.toggle_lighting();
    if (hview != nullptr) {
      CheckMenuItem(hview, static_cast<UINT>(kMenuGlobeLighting),
                    MF_BYCOMMAND | (sh.globe_.lighting_enabled() ? MF_CHECKED : MF_UNCHECKED));
    }
  } else if (cmd == kMenuSplitScaleSync) {
    sh.split_scale_sync_enabled_ = !sh.split_scale_sync_enabled_;
    if (hview != nullptr) {
      CheckMenuItem(hview, static_cast<UINT>(kMenuSplitScaleSync),
                    MF_BYCOMMAND | (sh.split_scale_sync_enabled_ ? MF_CHECKED : MF_UNCHECKED));
    }
  } else if (cmd == kMenuTacticalAutoBounds) {
    const bool v = !sh.tactical_.auto_bounds_include_entities();
    sh.tactical_.set_auto_bounds_include_entities(v);
    if (hview != nullptr) {
      CheckMenuItem(hview, static_cast<UINT>(kMenuTacticalAutoBounds),
                    MF_BYCOMMAND | (v ? MF_CHECKED : MF_UNCHECKED));
    }
  } else if (cmd == kMenuSimPause) {
    if (engine_ != nullptr) {
      static_cast<void>(engine_->pause());
    }
  } else if (cmd == kMenuSimResume) {
    if (engine_ != nullptr) {
      static_cast<void>(engine_->start());
    }
  } else if (cmd == kMenuSimEnd) {
    if (engine_ != nullptr) {
      static_cast<void>(engine_->end());
    }
  } else if (cmd == kMenuSimReset) {
    if (engine_ != nullptr && scenario_ != nullptr) {
      static_cast<void>(engine_->reset_with_scenario(*scenario_));
    }
  } else if (cmd >= kMenuSimSpeed025 && cmd <= kMenuSimSpeed4) {
    if (engine_ != nullptr) {
      static constexpr double kScales[] = {0.25, 0.5, 1.0, 2.0, 4.0};
      const unsigned idx = cmd - kMenuSimSpeed025;
      if (idx < sizeof(kScales) / sizeof(kScales[0])) {
        static_cast<void>(engine_->set_time_scale(kScales[idx]));
      }
      HMENU hsim = static_cast<HMENU>(hmenu_sim_);
      if (hsim != nullptr) {
        CheckMenuRadioItem(hsim, kMenuSimSpeed025, kMenuSimSpeed4, static_cast<UINT>(cmd), MF_BYCOMMAND);
      }
    }
  } else if (cmd == kMenuGfxOpenGL) {
    if (hmenu_gfx_ != nullptr) {
      CheckMenuRadioItem(static_cast<HMENU>(hmenu_gfx_), kMenuGfxOpenGL, kMenuGfxVulkan, kMenuGfxOpenGL,
                         MF_BYCOMMAND);
    }
    if (gfx_api_handler_) {
      gfx_api_handler_(cw::render::GraphicsApi::OpenGL);
    }
  } else if (cmd == kMenuGfxVulkan) {
    if (hmenu_gfx_ != nullptr) {
      CheckMenuRadioItem(static_cast<HMENU>(hmenu_gfx_), kMenuGfxOpenGL, kMenuGfxVulkan, kMenuGfxVulkan,
                         MF_BYCOMMAND);
    }
    if (gfx_api_handler_) {
      gfx_api_handler_(cw::render::GraphicsApi::Vulkan);
    }
  }
}

void Win32SituationChrome::install_view_menu(cw::render::GlWindow& win, SituationViewShell& shell) {
  shell_ = &shell;
  hwnd_main_ = win.native_menu_host_handle();
  HMENU h_bar = CreateMenu();
  HMENU h_view = CreateMenu();
  hmenu_view_ = h_view;
  AppendMenuW(h_view, MF_STRING | MF_CHECKED, static_cast<UINT_PTR>(kMenuView2d), L"2D tactical map");
  AppendMenuW(h_view, MF_STRING, static_cast<UINT_PTR>(kMenuView3d), L"3D globe");
  AppendMenuW(h_view, MF_STRING, static_cast<UINT_PTR>(kMenuViewSplit2d3d), L"2D + 3D split view");
  AppendMenuW(h_view, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(h_view, MF_STRING, static_cast<UINT_PTR>(kMenuGlobeLighting), L"三维地图光照");
  AppendMenuW(h_view, MF_STRING | MF_CHECKED, static_cast<UINT_PTR>(kMenuSplitScaleSync),
              L"分屏左右比例尺同步");
  AppendMenuW(h_view, MF_STRING, static_cast<UINT_PTR>(kMenuTacticalAutoBounds),
              L"战术自动框选实体");
  AppendMenuW(h_view, MF_SEPARATOR, 0, nullptr);
  HMENU h_gfx = CreateMenu();
  AppendMenuW(h_gfx, MF_STRING, static_cast<UINT_PTR>(kMenuGfxOpenGL), L"OpenGL");
  AppendMenuW(h_gfx, MF_STRING, static_cast<UINT_PTR>(kMenuGfxVulkan), L"Vulkan");
  AppendMenuW(h_view, MF_POPUP, reinterpret_cast<UINT_PTR>(h_gfx), L"Graphics API");
  hmenu_gfx_ = h_gfx;
  {
    const unsigned gfx_chk =
        (win.window_graphics_api() == cw::render::GraphicsApi::Vulkan) ? kMenuGfxVulkan : kMenuGfxOpenGL;
    CheckMenuRadioItem(h_gfx, kMenuGfxOpenGL, kMenuGfxVulkan, gfx_chk, MF_BYCOMMAND);
  }
  AppendMenuW(h_bar, MF_POPUP, reinterpret_cast<UINT_PTR>(h_view), L"View");
  SetMenu(static_cast<HWND>(hwnd_main_), h_bar);
  DrawMenuBar(static_cast<HWND>(hwnd_main_));
  win.set_menu_command_callback(&Win32SituationChrome::menu_thunk, this);
}

void Win32SituationChrome::install_simulation_menu(cw::render::GlWindow& win, SituationViewShell& shell) {
  shell_ = &shell;
  hwnd_main_ = win.native_menu_host_handle();
  HMENU h_bar = GetMenu(static_cast<HWND>(hwnd_main_));
  if (h_bar == nullptr) {
    return;
  }
  HMENU h_sim = CreateMenu();
  hmenu_sim_ = h_sim;
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimPause), L"Pause");
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimResume), L"Resume");
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimEnd), L"End simulation");
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimReset), L"Reset simulation");
  AppendMenuW(h_sim, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimSpeed025), L"Time scale 0.25x");
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimSpeed05), L"Time scale 0.5x");
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimSpeed1), L"Time scale 1x");
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimSpeed2), L"Time scale 2x");
  AppendMenuW(h_sim, MF_STRING, static_cast<UINT_PTR>(kMenuSimSpeed4), L"Time scale 4x");
  CheckMenuRadioItem(h_sim, kMenuSimSpeed025, kMenuSimSpeed4, kMenuSimSpeed1, MF_BYCOMMAND);
  AppendMenuW(h_bar, MF_POPUP, reinterpret_cast<UINT_PTR>(h_sim), L"Simulation");
  DrawMenuBar(static_cast<HWND>(hwnd_main_));
}

void Win32SituationChrome::set_simulation_targets(cw::engine::Engine* engine,
                                                  const cw::scenario::Scenario* scenario) noexcept {
  engine_ = engine;
  scenario_ = scenario;
}

void Win32SituationChrome::sync_simulation_menu_from_engine() {
  if (engine_ == nullptr || hmenu_sim_ == nullptr) {
    return;
  }
  HMENU h_sim = static_cast<HMENU>(hmenu_sim_);
  const double ts = engine_->time_scale();
  static constexpr double kScales[] = {0.25, 0.5, 1.0, 2.0, 4.0};
  unsigned cmd = kMenuSimSpeed1;
  for (unsigned i = 0; i < sizeof(kScales) / sizeof(kScales[0]); ++i) {
    if (std::fabs(ts - kScales[i]) < 1e-5) {
      cmd = kMenuSimSpeed025 + i;
      break;
    }
  }
  CheckMenuRadioItem(h_sim, kMenuSimSpeed025, kMenuSimSpeed4, cmd, MF_BYCOMMAND);
}

void Win32SituationChrome::set_graphics_api_switch_handler(std::function<void(cw::render::GraphicsApi)> fn) noexcept {
  gfx_api_handler_ = std::move(fn);
}

void Win32SituationChrome::sync_graphics_api_menu(cw::render::GraphicsApi current) noexcept {
  if (hmenu_gfx_ == nullptr) {
    return;
  }
  HMENU g = static_cast<HMENU>(hmenu_gfx_);
  const unsigned id =
      (current == cw::render::GraphicsApi::Vulkan) ? kMenuGfxVulkan : kMenuGfxOpenGL;
  CheckMenuRadioItem(g, kMenuGfxOpenGL, kMenuGfxVulkan, id, MF_BYCOMMAND);
}

}  // namespace cw::situation_view
