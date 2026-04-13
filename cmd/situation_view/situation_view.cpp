#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <GL/gl.h>

#include "cw/engine/engine.hpp"
#include "cw/error.hpp"
#include "cw/log.hpp"
#include "cw/render/gl_window.hpp"
#ifdef _WIN32
#include "cw/render/gl_offscreen_win32.hpp"
#endif
#include "cw/render/graphics_device.hpp"
#include "cw/render/graphics_types.hpp"
#include "cw/render/globe_program.hpp"
#include "cw/render/world_vector_lines.hpp"
#include "cw/scenario/parse.hpp"
#include "cw/situation_view/asset_paths.hpp"
#include "cw/situation_view/icon_texture_cache.hpp"
#include "cw/situation_view/situation_map_globe_render.hpp"
#include "cw/situation_view/situation_view_chrome.hpp"
#include "cw/situation_view/situation_view_shell.hpp"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

void check(cw::Error e, const char* what) {
  if (!cw::ok(e)) {
    std::string msg = "situation_view: ";
    msg += what;
    msg += " [";
    msg += cw::error_code_str(e);
    msg += "] ";
    msg += cw::error_message(e);
    cw::log(cw::LogLevel::Error, msg);
    std::exit(EXIT_FAILURE);
  }
}

void adjust_engine_time_scale_by_step(cw::engine::Engine& e, int dir) {
  static constexpr double kScales[] = {0.25, 0.5, 1.0, 2.0, 4.0};
  constexpr int n = 5;
  const double cur = e.time_scale();
  int best = 0;
  double best_d = 1e100;
  for (int i = 0; i < n; ++i) {
    const double d = std::fabs(kScales[i] - cur);
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  const int ni = std::clamp(best + dir, 0, n - 1);
  static_cast<void>(e.set_time_scale(kScales[ni]));
}

#ifdef _WIN32
struct SituationViewCli {
  bool vk_native_clear = false;
  const char* scen_path_arg = nullptr;
};

[[nodiscard]] SituationViewCli parse_situation_view_cli(int argc, char** argv) noexcept {
  SituationViewCli cli{};
  for (int ai = 1; ai < argc; ++ai) {
    if (argv[ai] == nullptr || argv[ai][0] == '\0') {
      continue;
    }
    if (std::strcmp(argv[ai], "--vk-native") == 0) {
      cli.vk_native_clear = true;
      continue;
    }
    if (argv[ai][0] == '-') {
      continue;
    }
    cli.scen_path_arg = argv[ai];
    break;
  }
  return cli;
}

[[nodiscard]] cw::scenario::Scenario load_scenario_from_cli(const SituationViewCli& cli, bool* map_only) {
  *map_only = cli.scen_path_arg == nullptr;
  cw::scenario::Scenario sc{};
  if (*map_only) {
    sc.version = 2;
  } else {
    const std::string scen_path = cw::situation_view::resolve_asset_path_utf8(cli.scen_path_arg);
    cw::situation_view::set_scenario_directory_for_asset_search_utf8(scen_path);
    check(cw::scenario::parse_scenario_file(scen_path.c_str(), sc), "parse_scenario_file");
  }
  return sc;
}

[[nodiscard]] std::string window_title_utf8(bool map_only, const char* scen_path_arg) {
  return map_only ? std::string("Clockwork — map")
                  : (std::string("Clockwork — ").append(scen_path_arg != nullptr ? scen_path_arg : ""));
}

void load_optional_coastlines(cw::render::WorldVectorLines& coastlines) {
  const auto loaded = cw::situation_view::try_resolved_asset_candidates(
      {"assets/maps/world_coastlines.mercl",
       "../assets/maps/world_coastlines.mercl",
       "../../assets/maps/world_coastlines.mercl",
       "assets/maps/10m_physical/world_coastlines.mercl",
       "../assets/maps/10m_physical/world_coastlines.mercl",
       "../../assets/maps/10m_physical/world_coastlines.mercl"},
      [&](const std::string& p) { return coastlines.load_from_file(p.c_str()); });
  if (loaded) {
    cw::log(cw::LogLevel::Info, std::string("situation_view: loaded coastline lines ").append(*loaded));
  } else {
    cw::log(cw::LogLevel::Info,
            "situation_view: coastlines missing (run: python scripts/build_boundary_lines_mercl.py "
            "-i assets/maps/10m_physical/ne_10m_coastline.shp -o assets/maps/world_coastlines.mercl)");
  }
}

void load_optional_boundary_lines(cw::render::WorldVectorLines& boundary_lines) {
  const auto loaded = cw::situation_view::try_resolved_asset_candidates(
      {"assets/maps/world_boundary_lines.mercl",
       "../assets/maps/world_boundary_lines.mercl",
       "../../assets/maps/world_boundary_lines.mercl",
       "assets/maps/10m_physical/world_boundary_lines.mercl",
       "../assets/maps/10m_physical/world_boundary_lines.mercl",
       "../../assets/maps/10m_physical/world_boundary_lines.mercl"},
      [&](const std::string& p) { return boundary_lines.load_from_file(p.c_str()); });
  if (loaded) {
    cw::log(cw::LogLevel::Info, std::string("situation_view: loaded boundary lines ").append(*loaded));
  } else {
    cw::log(cw::LogLevel::Info,
            "situation_view: boundary lines missing (run: python "
            "scripts/build_boundary_lines_mercl.py). Note: NE land borders are in 10m_cultural, "
            "not 10m_physical; use -i path/to/ne_10m_admin_0_boundary_lines_land.shp or geojson");
  }
}

[[nodiscard]] GLuint init_hud_font_and_globe_program(bool gl_for_scene, bool vulkan_present,
                                                     cw::render::GlOffscreenWin32* offscreen,
                                                     cw::render::GraphicsDevice& gfx, cw::render::GlWindow& win) {
  GLuint hud_font_base = 0;
  if (!gl_for_scene) {
    return 0;
  }
  if (vulkan_present) {
    offscreen->make_current();
  } else {
    gfx.make_current();
  }
  hud_font_base = static_cast<GLuint>(win.create_hud_bitmap_font_lists());
  if (hud_font_base == 0) {
    cw::log(cw::LogLevel::Info, "situation_view: bitmap font for HUD unavailable");
  }
  if (!cw::render::globe_program_try_init()) {
    cw::log(cw::LogLevel::Info, "situation_view: GLSL globe not available (fallback to gluSphere)");
  }
  return hud_font_base;
}

void teardown_session_gl_and_map_assets(bool gl_for_scene, bool vulkan_present,
                                          cw::render::GlOffscreenWin32* offscreen,
                                          cw::render::GraphicsDevice& gfx, cw::render::GlWindow& win,
                                          GLuint hud_font_base, cw::render::WorldVectorLines& coastlines,
                                          cw::render::WorldVectorLines& boundary_lines,
                                          cw::situation_view::IconTextureCache& entity_icons) {
  if (gl_for_scene) {
    if (vulkan_present) {
      offscreen->make_current();
    } else {
      gfx.make_current();
    }
    if (hud_font_base != 0) {
      win.destroy_hud_bitmap_font_lists(static_cast<unsigned>(hud_font_base), 96);
    }
    cw::render::globe_program_shutdown();
    coastlines.destroy();
    boundary_lines.destroy();
    entity_icons.destroy_all();
  } else {
    coastlines.destroy();
    boundary_lines.destroy();
    entity_icons.destroy_all();
  }
}

void run_win32_session_frame_loop(cw::engine::Engine& engine, cw::situation_view::SituationViewShell& shell,
                                  cw::situation_view::SituationViewChrome& chrome, cw::render::GlWindow& win,
                                  cw::render::GraphicsDevice& gfx, cw::render::GlOffscreenWin32* offscreen,
                                  bool map_only, bool gl_for_scene, bool vulkan_present,
                                  cw::situation_view::SituationRenderOptions& render_opts,
                                  cw::render::WorldVectorLines& coastlines,
                                  cw::render::WorldVectorLines& boundary_lines,
                                  cw::situation_view::IconTextureCache& entity_icons, GLuint hud_font_base,
                                  std::atomic<bool>& want_api_switch, cw::render::GraphicsApi& pending_api) {
  LARGE_INTEGER freq{};
  LARGE_INTEGER prev{};
  LARGE_INTEGER now{};
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&prev);

  /// 固定步长仿真：将墙钟时间欠账跨帧累积，否则在 `time_scale==1` 且 `dt` 略小于 `fixed_step` 时整帧无 `step()`，
  /// 表现为 1x 不推进、倍速反而正常。
  double sim_time_debt = 0.0;

  bool prev_left_down = false;
  double fps_ema = 0.0;

  std::vector<unsigned char> vulkan_readback;
  int vulkan_rb_last_w = 0;
  int vulkan_rb_last_h = 0;

  while (win.is_open() && !win.should_close()) {
    win.poll_events();
    gfx.begin_frame();

    const int cw = win.client_width();
    const int ch = win.client_height();

    bool split_left_driven = false;
    bool split_right_driven = false;

    /// 拖动平移在 `step()` 之前执行，外包络与上一帧末态势一致（与原 `Engine&` 语义相同）。
    shell.process_mouse_drag(win, engine.situation_presentation(), split_left_driven, split_right_driven);

    QueryPerformanceCounter(&now);
    const double dt =
        static_cast<double>(now.QuadPart - prev.QuadPart) / static_cast<double>(freq.QuadPart);
    prev = now;

    const double dt_safe = std::max(dt, 1e-9);
    const double frame_ms_wall = dt * 1000.0;
    fps_ema = (fps_ema <= 0.0) ? (1.0 / dt_safe) : (fps_ema * 0.92 + (1.0 / dt_safe) * 0.08);

    const double step = engine.fixed_step();
    constexpr int kMaxStepsPerFrame = 8;
    if (engine.state() == cw::engine::EngineState::Running) {
      sim_time_debt += dt * engine.time_scale();
      const double max_carry = step * static_cast<double>(kMaxStepsPerFrame);
      if (sim_time_debt > max_carry) {
        sim_time_debt = max_carry;
      }
    } else {
      sim_time_debt = 0.0;
    }
    int n = 0;
    while (sim_time_debt >= step && n < kMaxStepsPerFrame &&
           engine.state() == cw::engine::EngineState::Running) {
      engine.step();
      sim_time_debt -= step;
      ++n;
    }

    const cw::engine::SituationPresentation world = engine.situation_presentation();

    shell.process_wheel(win, split_left_driven, split_right_driven);

    if (gl_for_scene && vulkan_present) {
      offscreen->make_current();
    } else if (gl_for_scene) {
      gfx.make_current();
    }
    if (gl_for_scene && !map_only) {
      const bool left_down = win.left_button_down();
      shell.process_entity_pick_mouse(world, cw, ch, win.mouse_client_x(), win.mouse_client_y(), left_down,
                                      prev_left_down);
      prev_left_down = left_down;
    }
    gfx.set_viewport(0, 0, cw, ch);
    if (gl_for_scene) {
      if (vulkan_present) {
        offscreen->make_current();
        if (offscreen->ensure_framebuffer(cw, ch)) {
          offscreen->bind_draw_framebuffer();
          glViewport(0, 0, cw, ch);
          if (offscreen->read_async_capable()) {
            if (vulkan_rb_last_w > 0 && vulkan_rb_last_h > 0) {
              vulkan_readback.resize(static_cast<std::size_t>(vulkan_rb_last_w) *
                                     static_cast<std::size_t>(vulkan_rb_last_h) * 4U);
              int rw = 0;
              int rh = 0;
              if (offscreen->try_resolve_read_pixels_bgra_tight(vulkan_readback.data(), &rw, &rh)) {
                gfx.upload_swapchain_from_cpu_bgra(rw, rh, static_cast<std::size_t>(rw) * 4U,
                                                   vulkan_readback.data());
              }
            }
          }
          shell.pre_draw_split_sync(world, cw, ch, split_left_driven, split_right_driven);
          cw::situation_view::draw_frame(world, shell, cw, ch, win.mouse_client_x(), win.mouse_client_y(), nullptr, 0U,
                                         coastlines.valid() ? &coastlines : nullptr,
                                         boundary_lines.valid() ? &boundary_lines : nullptr, entity_icons, !map_only,
                                         nullptr, hud_font_base, render_opts, fps_ema, frame_ms_wall, gfx.api());
          if (offscreen->read_async_capable()) {
            offscreen->commit_read_pixels_bgra_async(cw, ch);
            vulkan_rb_last_w = cw;
            vulkan_rb_last_h = ch;
          } else {
            vulkan_readback.resize(static_cast<std::size_t>(cw) * static_cast<std::size_t>(ch) * 4U);
            offscreen->read_pixels_bgra_tight(cw, ch, vulkan_readback.data());
            gfx.upload_swapchain_from_cpu_bgra(cw, ch, static_cast<std::size_t>(cw) * 4U, vulkan_readback.data());
            vulkan_rb_last_w = 0;
            vulkan_rb_last_h = 0;
          }
        }
      } else {
        shell.pre_draw_split_sync(world, cw, ch, split_left_driven, split_right_driven);
        cw::situation_view::draw_frame(world, shell, cw, ch, win.mouse_client_x(), win.mouse_client_y(), nullptr, 0U,
                                       coastlines.valid() ? &coastlines : nullptr,
                                       boundary_lines.valid() ? &boundary_lines : nullptr, entity_icons, !map_only,
                                       nullptr, hud_font_base, render_opts, fps_ema, frame_ms_wall, gfx.api());
      }
    }
    if (vulkan_present && gfx.vulkan_native_scene_clear_only()) {
      gfx.set_vulkan_native_scene_anim_param(static_cast<float>(world.situation.sim_time));
    }
    gfx.end_frame();
    gfx.present();

    if (!map_only) {
      chrome.sync_simulation_menu_from_engine();
    }
    chrome.sync_graphics_api_menu(gfx.api());
    if (want_api_switch.load(std::memory_order_acquire) && pending_api != gfx.api()) {
      break;
    }
    const cw::render::GlWindowHotkeyEdges hk = win.poll_hotkey_edges();
    if (hk.home_reset_view) {
      shell.reset_view_camera();
    }
    if (hk.toggle_pause) {
      if (engine.state() == cw::engine::EngineState::Running) {
        check(engine.pause(), "pause");
      } else if (engine.state() == cw::engine::EngineState::Paused) {
        check(engine.start(), "start");
      }
    }
    if (!map_only) {
      if (hk.time_scale_up) {
        adjust_engine_time_scale_by_step(engine, +1);
      }
      if (hk.time_scale_down) {
        adjust_engine_time_scale_by_step(engine, -1);
      }
    }
    if (hk.escape) {
      break;
    }
  }
}

/// Returns true to `continue` the outer graphics restart loop (API switch path), false to exit that loop.
[[nodiscard]] bool try_continue_after_session(cw::render::GlWindow& win, bool cli_vulkan_native_clear,
                                              std::atomic<bool>& want_api_switch,
                                              cw::render::GraphicsApi& pending_api,
                                              cw::render::GraphicsApi& session_api, cw::render::GraphicsApi used_api,
                                              cw::situation_view::SituationViewChrome& chrome) {
  const bool switching =
      want_api_switch.exchange(false, std::memory_order_acq_rel) && (pending_api != used_api);
  if (switching) {
    win.set_vulkan_disable_offscreen_gl(cli_vulkan_native_clear &&
                                        (pending_api == cw::render::GraphicsApi::Vulkan));
    if (win.try_set_window_graphics_api(pending_api)) {
      session_api = pending_api;
      return true;
    }
    cw::log(cw::LogLevel::Error,
            "situation_view: Graphics API switch failed; staying on previous backend");
    pending_api = session_api;
    chrome.sync_graphics_api_menu(used_api);
    return true;
  }
  win.close();
  return false;
}

[[nodiscard]] int run_situation_view_win32_main(int argc, char** argv) {
  const SituationViewCli cli = parse_situation_view_cli(argc, argv);
  bool map_only = false;
  cw::scenario::Scenario sc = load_scenario_from_cli(cli, &map_only);

  cw::engine::Engine engine;
  engine.set_fixed_step(1.0 / 60.0);
  check(engine.initialize(), "initialize");
  check(engine.apply_scenario(sc), "apply_scenario");
  check(engine.start(), "start");

  const std::string title = window_title_utf8(map_only, cli.scen_path_arg);

  cw::situation_view::SituationViewShell shell;
  shell.set_viewport_sync_engine(&engine);
  std::unique_ptr<cw::situation_view::SituationViewChrome> chrome =
      cw::situation_view::create_situation_view_chrome();

  cw::render::GraphicsApi session_api = cw::render::GraphicsApi::OpenGL;
  cw::render::GraphicsApi pending_api = session_api;
  std::atomic<bool> want_api_switch{false};
  chrome->set_graphics_api_switch_handler([&](cw::render::GraphicsApi a) {
    pending_api = a;
    want_api_switch.store(true, std::memory_order_release);
  });

  std::unique_ptr<cw::render::GlWindow> win = cw::render::create_gl_window();
  cw::render::GlWindowConfig win_cfg{};
  win_cfg.width = 1280;
  win_cfg.height = 720;
  win_cfg.title_utf8 = title.c_str();
  win_cfg.window_graphics_api = session_api;
  win_cfg.vulkan_disable_offscreen_gl =
      cli.vk_native_clear && (session_api == cw::render::GraphicsApi::Vulkan);
  if (!win->open(win_cfg)) {
    cw::log(cw::LogLevel::Error, "situation_view: GlWindow::open failed (Windows only in phase 4)");
    return EXIT_FAILURE;
  }

  chrome->install_view_menu(*win, shell);
  if (!map_only) {
    chrome->set_simulation_targets(&engine, &sc);
    chrome->install_simulation_menu(*win, shell);
  }

  for (;;) {
    std::unique_ptr<cw::render::GraphicsDevice> gfx = cw::render::create_graphics_device_for_window(*win);
    if (!gfx) {
      if (session_api == cw::render::GraphicsApi::Vulkan) {
        if (win->try_set_window_graphics_api(cw::render::GraphicsApi::OpenGL)) {
          cw::log(cw::LogLevel::Info, "situation_view: Vulkan unavailable, falling back to OpenGL on same window");
          session_api = cw::render::GraphicsApi::OpenGL;
          pending_api = session_api;
          want_api_switch.store(false, std::memory_order_release);
          continue;
        }
      }
      win->close();
      return EXIT_FAILURE;
    }

    win->sync_client_size_from_window();

    const bool vulkan_present = (gfx->api() == cw::render::GraphicsApi::Vulkan);
    if (vulkan_present) {
      gfx->set_vulkan_native_scene_clear_only(cli.vk_native_clear);
      if (cli.vk_native_clear) {
        cw::log(cw::LogLevel::Info,
                "situation_view: Vulkan native scene (--vk-native): swapchain clear only, no GL map. "
                "Omit flag to restore GL offscreen + composite.");
      }
    }
    cw::render::GlOffscreenWin32* const offscreen = vulkan_present ? win->offscreen_gl() : nullptr;
    if (vulkan_present && offscreen == nullptr && !gfx->vulkan_native_scene_clear_only()) {
      cw::log(cw::LogLevel::Error, "situation_view: Vulkan mode requires offscreen OpenGL (implementation missing)");
      gfx.reset();
      win->close();
      return EXIT_FAILURE;
    }
    const bool vulkan_native_clear = gfx->vulkan_native_scene_clear_only();
    const bool gl_for_scene = !vulkan_present || (!vulkan_native_clear && offscreen != nullptr);

    const GLuint hud_font_base =
        init_hud_font_and_globe_program(gl_for_scene, vulkan_present, offscreen, *gfx, *win);

    cw::situation_view::SituationRenderOptions render_opts{};
    // 与原先一致：false 时仅洋面 + 岸线/国界等。
    render_opts.show_land_basemap = false;

    cw::situation_view::IconTextureCache entity_icons{};

    cw::render::WorldVectorLines coastlines{};
    load_optional_coastlines(coastlines);

    cw::render::WorldVectorLines boundary_lines{};
    load_optional_boundary_lines(boundary_lines);

    run_win32_session_frame_loop(engine, shell, *chrome, *win, *gfx, offscreen, map_only, gl_for_scene,
                                 vulkan_present, render_opts, coastlines, boundary_lines, entity_icons, hud_font_base,
                                 want_api_switch, pending_api);

    const cw::render::GraphicsApi used_api = gfx->api();
    teardown_session_gl_and_map_assets(gl_for_scene, vulkan_present, offscreen, *gfx, *win, hud_font_base, coastlines,
                                       boundary_lines, entity_icons);
    gfx.reset();

    if (try_continue_after_session(*win, cli.vk_native_clear, want_api_switch, pending_api, session_api, used_api,
                                   *chrome)) {
      continue;
    }
    break;
  }

  check(engine.end(), "end");
  cw::log(cw::LogLevel::Info, "situation_view: exit");
  return EXIT_SUCCESS;
}
#endif

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  return run_situation_view_win32_main(argc, argv);
#else
  cw::log(cw::LogLevel::Error, "situation_view: phase 4 viewer is implemented for Windows only");
  return EXIT_FAILURE;
#endif
}
