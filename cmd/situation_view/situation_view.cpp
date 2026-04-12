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
#include "cw/log.hpp"
#include "cw/render/gl_window.hpp"
#include "cw/render/globe_program.hpp"
#include "cw/render/texture_bmp.hpp"
#include "cw/render/world_vector_lines.hpp"
#include "cw/render/world_vector_merc.hpp"
#include "cw/scenario/parse.hpp"
#include "cw/situation_view/asset_paths.hpp"
#include "cw/situation_view/icon_texture_cache.hpp"
#include "cw/situation_view/situation_map_globe_render.hpp"
#include "cw/situation_view/situation_view_shell.hpp"

#include <cstdlib>
#include <string>

namespace {

void check(cw::Error e, const char* what) {
  if (!cw::ok(e)) {
    cw::log(cw::LogLevel::Error, std::string("situation_view: ")
                                    .append(what)
                                    .append(" err=")
                                    .append(std::to_string(static_cast<int>(e))));
    std::exit(EXIT_FAILURE);
  }
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  const bool map_only = argc < 2 || argv[1] == nullptr || argv[1][0] == '\0';

  cw::scenario::Scenario sc{};
  if (map_only) {
    sc.version = 2;
  } else {
    const std::string scen_path = cw::situation_view::resolve_asset_path_utf8(argv[1]);
    cw::situation_view::set_scenario_directory_for_asset_search_utf8(scen_path);
    check(cw::scenario::parse_scenario_file(scen_path.c_str(), sc), "parse_scenario_file");
  }

  cw::engine::Engine engine;
  engine.set_fixed_step(1.0 / 60.0);
  check(engine.initialize(), "initialize");
  check(engine.apply_scenario(sc), "apply_scenario");
  check(engine.start(), "start");

  cw::render::GlWindow win;
  const std::string title =
      map_only ? std::string("Clockwork — map")
               : (std::string("Clockwork — ").append(argv[1] != nullptr ? argv[1] : ""));
  if (!win.open({1280, 720, title.c_str()})) {
    cw::log(cw::LogLevel::Error, "situation_view: GlWindow::open failed (Windows only in phase 4)");
    return EXIT_FAILURE;
  }

  cw::situation_view::SituationViewShell shell;
  shell.install_win32_view_menu(win);

  GLuint hud_font_base = 0;
  {
    HDC hdc = static_cast<HDC>(win.win32_hdc());
    hud_font_base = cw::situation_view::create_hud_bitmap_font_lists(hdc);
    if (hud_font_base == 0) {
      cw::log(cw::LogLevel::Info, "situation_view: bitmap font for HUD unavailable");
    }
  }

  win.make_current();
  if (!cw::render::globe_program_try_init()) {
    cw::log(cw::LogLevel::Info, "situation_view: GLSL globe not available (fallback to gluSphere)");
  }

  cw::situation_view::SituationRenderOptions render_opts{};
  // 与原先一致：false 时仅洋面 + 岸线/国界等。
  render_opts.show_land_basemap = false;

  cw::render::WorldVectorMerc world_vec{};
  const char* const kVectorCandidates[] = {
      "assets/maps/world_land.merc2",
      "../assets/maps/world_land.merc2",
      "../../assets/maps/world_land.merc2",
  };
  const char* vector_loaded_from = nullptr;
  for (const char* rel : kVectorCandidates) {
    const std::string p = cw::situation_view::resolve_asset_path_utf8(rel);
    if (world_vec.load_from_file(p.c_str())) {
      vector_loaded_from = p.c_str();
      break;
    }
  }
  if (vector_loaded_from != nullptr) {
    cw::log(cw::LogLevel::Info,
            std::string("situation_view: loaded vector land ").append(vector_loaded_from));
  } else {
    cw::log(cw::LogLevel::Info,
            "situation_view: optional vector land not found (assets/maps/world_land.merc2). "
            "Generate from repo root: python scripts/build_world_vector_merc.py "
            "(downloads Natural Earth 110m land GeoJSON if needed). "
            "Otherwise raster BMP or ocean-only underlay is used.");
  }

  if (render_opts.show_land_basemap && vector_loaded_from != nullptr && cw::render::globe_program_ready()) {
    if (cw::render::globe_merc_atlas_build_from_vector_land(world_vec)) {
      cw::log(cw::LogLevel::Info, "situation_view: GLSL globe mercator land atlas ready");
    } else {
      cw::log(cw::LogLevel::Info,
              "situation_view: mercator land atlas build failed (3D uses tessellated land fill)");
    }
  }

  cw::render::Texture2DRgb world_tex{};
  const char* map_loaded_from = nullptr;
  if (vector_loaded_from == nullptr) {
    const char* const kMapCandidates[] = {
        "assets/maps/world_equirect_4096x2048.bmp",
        "../assets/maps/world_equirect_4096x2048.bmp",
        "../../assets/maps/world_equirect_4096x2048.bmp",
        "assets/maps/world_equirect_2048x1024.bmp",
        "../assets/maps/world_equirect_2048x1024.bmp",
        "../../assets/maps/world_equirect_2048x1024.bmp",
        "assets/maps/world_equirect_1024x512.bmp",
        "../assets/maps/world_equirect_1024x512.bmp",
        "../../assets/maps/world_equirect_1024x512.bmp",
    };
    for (const char* rel : kMapCandidates) {
      const std::string p = cw::situation_view::resolve_asset_path_utf8(rel);
      if (cw::render::load_texture_bmp_rgb24(p.c_str(), world_tex)) {
        map_loaded_from = p.c_str();
        break;
      }
    }
    if (map_loaded_from != nullptr) {
      cw::log(cw::LogLevel::Info,
              std::string("situation_view: loaded raster basemap ").append(map_loaded_from));
    } else {
      cw::log(cw::LogLevel::Info,
              "situation_view: no basemap (vector or BMP); ocean/land background omitted");
    }
  }

  cw::situation_view::IconTextureCache entity_icons{};

  cw::render::WorldVectorLines coastlines{};
  const char* const kCoastCandidates[] = {
      "assets/maps/world_coastlines.mercl",
      "../assets/maps/world_coastlines.mercl",
      "../../assets/maps/world_coastlines.mercl",
      "assets/maps/10m_physical/world_coastlines.mercl",
      "../assets/maps/10m_physical/world_coastlines.mercl",
      "../../assets/maps/10m_physical/world_coastlines.mercl",
  };
  const char* coast_loaded_from = nullptr;
  for (const char* rel : kCoastCandidates) {
    const std::string p = cw::situation_view::resolve_asset_path_utf8(rel);
    if (coastlines.load_from_file(p.c_str())) {
      coast_loaded_from = p.c_str();
      break;
    }
  }
  if (coast_loaded_from != nullptr) {
    cw::log(cw::LogLevel::Info,
            std::string("situation_view: loaded coastline lines ").append(coast_loaded_from));
  } else {
    cw::log(cw::LogLevel::Info,
            "situation_view: coastlines missing (run: python scripts/build_boundary_lines_mercl.py "
            "-i assets/maps/10m_physical/ne_10m_coastline.shp -o assets/maps/world_coastlines.mercl)");
  }

  cw::render::WorldVectorLines boundary_lines{};
  const char* const kBoundaryCandidates[] = {
      "assets/maps/world_boundary_lines.mercl",
      "../assets/maps/world_boundary_lines.mercl",
      "../../assets/maps/world_boundary_lines.mercl",
      "assets/maps/10m_physical/world_boundary_lines.mercl",
      "../assets/maps/10m_physical/world_boundary_lines.mercl",
      "../../assets/maps/10m_physical/world_boundary_lines.mercl",
  };
  const char* boundary_loaded_from = nullptr;
  for (const char* rel : kBoundaryCandidates) {
    const std::string p = cw::situation_view::resolve_asset_path_utf8(rel);
    if (boundary_lines.load_from_file(p.c_str())) {
      boundary_loaded_from = p.c_str();
      break;
    }
  }
  if (boundary_loaded_from != nullptr) {
    cw::log(cw::LogLevel::Info,
            std::string("situation_view: loaded boundary lines ").append(boundary_loaded_from));
  } else {
    cw::log(cw::LogLevel::Info,
            "situation_view: boundary lines missing (run: python "
            "scripts/build_boundary_lines_mercl.py). Note: NE land borders are in 10m_cultural, "
            "not 10m_physical; use -i path/to/ne_10m_admin_0_boundary_lines_land.shp or geojson");
  }

  LARGE_INTEGER freq{};
  LARGE_INTEGER prev{};
  LARGE_INTEGER now{};
  QueryPerformanceFrequency(&freq);
  QueryPerformanceCounter(&prev);

  while (win.is_open() && !win.should_close()) {
    win.poll_events();

    bool split_left_driven = false;
    bool split_right_driven = false;

    const int cw = win.client_width();
    const int ch = win.client_height();
    shell.process_mouse_drag(win, engine, split_left_driven, split_right_driven);

    QueryPerformanceCounter(&now);
    const double dt =
        static_cast<double>(now.QuadPart - prev.QuadPart) / static_cast<double>(freq.QuadPart);
    prev = now;

    double debt = dt * engine.time_scale();
    const double step = engine.fixed_step();
    constexpr int kMaxStepsPerFrame = 8;
    int n = 0;
    while (debt >= step && n < kMaxStepsPerFrame && engine.state() == cw::engine::EngineState::Running) {
      engine.step();
      debt -= step;
      ++n;
    }

    shell.process_wheel(win, split_left_driven, split_right_driven);

#ifdef _WIN32
    win.make_current();
#endif
    glViewport(0, 0, cw, ch);
    shell.pre_draw_split_sync(engine, cw, ch, split_left_driven, split_right_driven);

    cw::situation_view::draw_frame(engine, shell, cw, ch, win.mouse_client_x(), win.mouse_client_y(),
                                   world_vec.valid() ? &world_vec : nullptr,
                                   world_tex.valid() ? world_tex.gl_name : 0U,
                                   coastlines.valid() ? &coastlines : nullptr,
                                   boundary_lines.valid() ? &boundary_lines : nullptr, entity_icons,
                                   !map_only, nullptr, hud_font_base, render_opts);
    win.swap_buffers();

#ifdef _WIN32
    if ((GetAsyncKeyState(VK_HOME) & 0x1) != 0) {
      shell.reset_view_camera();
    }
    if ((GetAsyncKeyState(VK_SPACE) & 0x1) != 0) {
      if (engine.state() == cw::engine::EngineState::Running) {
        check(engine.pause(), "pause");
      } else if (engine.state() == cw::engine::EngineState::Paused) {
        check(engine.start(), "start");
      }
    }
    if ((GetAsyncKeyState(VK_ESCAPE) & 0x1) != 0) {
      break;
    }
#endif
  }

  if (hud_font_base != 0) {
    glDeleteLists(hud_font_base, 96);
  }
  cw::render::globe_program_shutdown();
  coastlines.destroy();
  boundary_lines.destroy();
  world_vec.destroy();
  cw::render::destroy_texture_2d(world_tex);
  entity_icons.destroy_all();
  check(engine.end(), "end");
  win.close();
  cw::log(cw::LogLevel::Info, "situation_view: exit");
  return EXIT_SUCCESS;

#else
  cw::log(cw::LogLevel::Error, "situation_view: phase 4 viewer is implemented for Windows only");
  return EXIT_FAILURE;
#endif
}
