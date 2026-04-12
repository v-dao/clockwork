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
#include <GL/glu.h>

#include "cw/engine/engine.hpp"
#include "cw/log.hpp"
#include "cw/render/gl_window.hpp"
#include "cw/render/globe_program.hpp"
#include "cw/render/globe_view_3d.hpp"
#include "cw/render/lonlat_grid.hpp"
#include "cw/render/mercator_geo.hpp"
#include "cw/render/tactical_map_2d.hpp"
#include "cw/render/svg_line_texture.hpp"
#include "cw/render/texture_bmp.hpp"
#include "cw/render/world_vector_merc.hpp"
#include "cw/render/world_vector_lines.hpp"
#include "cw/scenario/parse.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

using cw::render::mercator_meters_to_lonlat;

#ifdef _WIN32
using GlWindowPos2fFn = void(APIENTRY*)(GLfloat, GLfloat);
GlWindowPos2fFn g_gl_window_pos_2f = nullptr;

void ensure_gl_window_pos_2f() {
  if (g_gl_window_pos_2f == nullptr) {
    auto* p = wglGetProcAddress("glWindowPos2f");
    g_gl_window_pos_2f = reinterpret_cast<GlWindowPos2fFn>(reinterpret_cast<void*>(p));
  }
}
#endif

/// 资源路径解析：想定文件所在目录的上一级通常即仓库根（scenarios/../assets/...）。
std::string g_scenario_file_dir{};

enum class ViewMode { Tactical2D, Globe3d, Split2dGlobe };
ViewMode g_view_mode = ViewMode::Tactical2D;

cw::render::TacticalMercatorMap g_tactical_map{};
cw::render::GlobeEarthView g_globe_view{};
/// false：仅洋面 + 岸线/国界等；不显示二维/三维陆地底色（矢量填充、等距柱 BMP、墨卡托陆栅格）。
constexpr bool k_show_land_basemap = false;

/// 切换到「2D+3D 分屏」后首帧：用战术图中心对齐地球 content_R，再参与双向同步。
bool g_split_initial_sync_pending = false;

void reset_globe_view_auxiliary_state() noexcept { g_globe_view.reset_content_orientation(); }

#ifdef _WIN32
constexpr unsigned kMenuView2d = 0xE100;
constexpr unsigned kMenuView3d = 0xE101;
constexpr unsigned kMenuViewSplit2d3d = 0xE103;
constexpr unsigned kMenuGlobeLighting = 0xE102;
HWND g_hwnd_main = nullptr;
HMENU g_hmenu_view = nullptr;

void on_view_menu(unsigned cmd, void* /*user*/) {
  if (cmd == kMenuView2d) {
    g_view_mode = ViewMode::Tactical2D;
    if (g_hmenu_view != nullptr) {
      CheckMenuRadioItem(g_hmenu_view, kMenuView2d, kMenuViewSplit2d3d, kMenuView2d, MF_BYCOMMAND);
    }
  } else if (cmd == kMenuView3d) {
    g_view_mode = ViewMode::Globe3d;
    if (g_hmenu_view != nullptr) {
      CheckMenuRadioItem(g_hmenu_view, kMenuView2d, kMenuViewSplit2d3d, kMenuView3d, MF_BYCOMMAND);
    }
  } else if (cmd == kMenuViewSplit2d3d) {
    g_view_mode = ViewMode::Split2dGlobe;
    g_split_initial_sync_pending = true;
    if (g_hmenu_view != nullptr) {
      CheckMenuRadioItem(g_hmenu_view, kMenuView2d, kMenuViewSplit2d3d, kMenuViewSplit2d3d, MF_BYCOMMAND);
    }
  } else if (cmd == kMenuGlobeLighting) {
    g_globe_view.toggle_lighting();
    if (g_hmenu_view != nullptr) {
      CheckMenuItem(g_hmenu_view, static_cast<UINT>(kMenuGlobeLighting),
                    MF_BYCOMMAND | (g_globe_view.lighting_enabled() ? MF_CHECKED : MF_UNCHECKED));
    }
  }
}
#endif

bool g_drag_prev_valid = false;
int g_drag_prev_mx = 0;
int g_drag_prev_my = 0;

void reset_view_camera() {
  g_tactical_map.reset_camera();
  g_drag_prev_valid = false;
  g_globe_view.reset_content_orientation();
}

void check(cw::Error e, const char* what) {
  if (!cw::ok(e)) {
    cw::log(cw::LogLevel::Error, std::string("situation_view: ")
                                    .append(what)
                                    .append(" err=")
                                    .append(std::to_string(static_cast<int>(e))));
    std::exit(EXIT_FAILURE);
  }
}

void draw_ocean_quad(const cw::render::MercatorOrthoFrustum& f) {
  glColor3f(0.078F, 0.227F, 0.463F);
  glBegin(GL_QUADS);
  glVertex2f(f.l, f.b);
  glVertex2f(f.r, f.b);
  glVertex2f(f.r, f.t);
  glVertex2f(f.l, f.t);
  glEnd();
}

void draw_world_underlay_vector(const cw::render::MercatorOrthoFrustum& f,
                                const cw::render::WorldVectorMerc* wv) {
  draw_ocean_quad(f);
  if (k_show_land_basemap && wv != nullptr && wv->valid()) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glColor4f(0.2F, 0.62F, 0.28F, 0.93F);
    const float W = cw::render::TacticalMercatorMap::kWorldWidthM;
    int k0 = static_cast<int>(std::floor(static_cast<double>(f.l) / static_cast<double>(W))) - 1;
    int k1 = static_cast<int>(std::ceil(static_cast<double>(f.r) / static_cast<double>(W))) + 1;
    if (k1 - k0 > 32) {
      k0 = -1;
      k1 = 1;
    }
    for (int k = k0; k <= k1; ++k) {
      glPushMatrix();
      glTranslatef(static_cast<float>(k) * W, 0.F, 0.F);
      wv->draw_land_fill();
      glPopMatrix();
    }
    glDisable(GL_BLEND);
    glColor4f(1.F, 1.F, 1.F, 1.F);
  }
}

void draw_world_vector_lines_tiled(const cw::render::WorldVectorLines* lines, float view_l, float view_r) {
  if (lines == nullptr || !lines->valid()) {
    return;
  }
  const float W = cw::render::TacticalMercatorMap::kWorldWidthM;
  int k0 = static_cast<int>(std::floor(static_cast<double>(view_l) / static_cast<double>(W))) - 1;
  int k1 = static_cast<int>(std::ceil(static_cast<double>(view_r) / static_cast<double>(W))) + 1;
  if (k1 - k0 > 32) {
    k0 = -1;
    k1 = 1;
  }
  for (int k = k0; k <= k1; ++k) {
    glPushMatrix();
    glTranslatef(static_cast<float>(k) * W, 0.F, 0.F);
    lines->draw();
    glPopMatrix();
  }
}

void draw_world_underlay(const cw::render::MercatorOrthoFrustum& f, unsigned tex_gl_name) {
  float u0 = 0.F;
  float v0 = 0.F;
  float u1 = 0.F;
  float v1 = 0.F;
  float u2 = 0.F;
  float v2 = 0.F;
  float u3 = 0.F;
  float v3 = 0.F;
  cw::render::TacticalMercatorMap::mercator_meters_to_tex_uv(static_cast<double>(f.l), static_cast<double>(f.b), u0,
                                                             v0);
  cw::render::TacticalMercatorMap::mercator_meters_to_tex_uv(static_cast<double>(f.r), static_cast<double>(f.b), u1,
                                                             v1);
  cw::render::TacticalMercatorMap::mercator_meters_to_tex_uv(static_cast<double>(f.r), static_cast<double>(f.t), u2,
                                                             v2);
  cw::render::TacticalMercatorMap::mercator_meters_to_tex_uv(static_cast<double>(f.l), static_cast<double>(f.t), u3,
                                                             v3);

  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex_gl_name));
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glColor4f(0.78F, 0.78F, 0.82F, 0.94F);
  glBegin(GL_QUADS);
  glTexCoord2f(u0, v0);
  glVertex2f(f.l, f.b);
  glTexCoord2f(u1, v1);
  glVertex2f(f.r, f.b);
  glTexCoord2f(u2, v2);
  glVertex2f(f.r, f.t);
  glTexCoord2f(u3, v3);
  glVertex2f(f.l, f.t);
  glEnd();
  glDisable(GL_BLEND);
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
  glColor4f(1.F, 1.F, 1.F, 1.F);
}

void draw_airspaces(const cw::engine::Engine& eng, float cx_ref) {
  glLineWidth(1.F);
  for (const auto& a : eng.airspaces()) {
    if (a.kind == cw::scenario::AirspaceKind::Box) {
      glColor3f(0.25F, 0.55F, 0.75F);
      glBegin(GL_LINE_LOOP);
      glVertex2f(cw::render::TacticalMercatorMap::mercator_periodic_x(a.box_min.x, cx_ref), a.box_min.y);
      glVertex2f(cw::render::TacticalMercatorMap::mercator_periodic_x(a.box_max.x, cx_ref), a.box_min.y);
      glVertex2f(cw::render::TacticalMercatorMap::mercator_periodic_x(a.box_max.x, cx_ref), a.box_max.y);
      glVertex2f(cw::render::TacticalMercatorMap::mercator_periodic_x(a.box_min.x, cx_ref), a.box_max.y);
      glEnd();
    } else if (a.polygon.size() >= 2) {
      glColor3f(0.25F, 0.55F, 0.75F);
      glBegin(GL_LINE_LOOP);
      for (const auto& p : a.polygon) {
        glVertex2f(cw::render::TacticalMercatorMap::mercator_periodic_x(p.x, cx_ref), p.y);
      }
      glEnd();
    }
  }
}

void draw_routes(const cw::engine::Engine& eng, float cx_ref) {
  glLineWidth(1.5F);
  glColor3f(0.55F, 0.55F, 0.6F);
  const float W = cw::render::TacticalMercatorMap::kWorldWidthM;
  for (const auto& r : eng.routes()) {
    if (r.waypoints.size() < 2) {
      continue;
    }
    glBegin(GL_LINE_STRIP);
    float px = cw::render::TacticalMercatorMap::mercator_periodic_x(r.waypoints[0].x, cx_ref);
    glVertex2f(px, r.waypoints[0].y);
    for (std::size_t i = 1; i < r.waypoints.size(); ++i) {
      float d = r.waypoints[i].x - r.waypoints[i - 1].x;
      d -= std::round(d / W) * W;
      px += d;
      glVertex2f(px, r.waypoints[i].y);
    }
    glEnd();
  }
}

void draw_detections(const cw::engine::Engine& eng,
                     const std::unordered_map<cw::engine::EntityId, cw::math::Vec3>& pos_by_id,
                     float cx_ref) {
  glLineWidth(1.F);
  glColor3f(0.95F, 0.85F, 0.2F);
  glEnable(GL_LINE_STIPPLE);
  glLineStipple(1, 0xAAAA);
  for (const auto& d : eng.situation().sensor_detections) {
    auto io = pos_by_id.find(d.observer_id);
    auto it = pos_by_id.find(d.target_id);
    if (io == pos_by_id.end() || it == pos_by_id.end()) {
      continue;
    }
    const float x0 = cw::render::TacticalMercatorMap::mercator_periodic_x(io->second.x, cx_ref);
    const float y0 = io->second.y;
    const float x1 = cw::render::TacticalMercatorMap::mercator_periodic_x(it->second.x, x0);
    const float y1 = it->second.y;
    glBegin(GL_LINES);
    glVertex2f(x0, y0);
    glVertex2f(x1, y1);
    glEnd();
  }
  glDisable(GL_LINE_STIPPLE);
}

bool ends_with_icase(const std::string& s, const char* suf) {
  const std::size_t n = std::strlen(suf);
  if (s.size() < n) {
    return false;
  }
  for (std::size_t i = 0; i < n; ++i) {
    if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) !=
        std::tolower(static_cast<unsigned char>(suf[i]))) {
      return false;
    }
  }
  return true;
}

std::string dirname_path_utf8(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  const std::size_t p = path.find_last_of("\\/");
  if (p == std::string::npos) {
    return {};
  }
  return path.substr(0, p);
}

/// 想定文件所在目录的绝对路径（与 cwd 无关），用于 assets/相对仓库根的解析。
std::string scenario_file_parent_absolute_utf8(const std::string& scen_path) {
  if (scen_path.empty()) {
    return {};
  }
  std::error_code ec;
  std::filesystem::path abs = std::filesystem::absolute(scen_path, ec);
  if (ec) {
    return dirname_path_utf8(scen_path);
  }
  abs = abs.lexically_normal();
  return abs.parent_path().generic_string();
}

void append_path_candidates(const std::string& rel, std::vector<std::string>& out) {
  if (!g_scenario_file_dir.empty()) {
    std::filesystem::path scen_dir(g_scenario_file_dir);
    std::filesystem::path root = (scen_dir / "..").lexically_normal();
    std::filesystem::path full = (root / rel).lexically_normal();
    out.push_back(full.generic_string());
  }
  out.push_back(rel);
  out.push_back(std::string("../") + rel);
  out.push_back(std::string("../../") + rel);
  // 常见：从 build/cmd/situation_view/ 启动时，仓库根在 ../../../
  out.push_back(std::string("../../../") + rel);
  out.push_back(std::string("../../../../") + rel);
#ifdef _WIN32
  // 自 exe 所在目录逐级向上拼接 rel，不依赖当前工作目录（IDE 常把 cwd 设为 build 目录）
  {
    wchar_t wbuf[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, wbuf, MAX_PATH) != 0U) {
      std::wstring wexe(wbuf);
      const auto cut = wexe.find_last_of(L"\\/");
      if (cut != std::wstring::npos) {
        std::wstring dir = wexe.substr(0, cut);
        for (int up = 0; up <= 20; ++up) {
          std::wstring wpath = dir;
          for (int i = 0; i < up; ++i) {
            const auto p = wpath.find_last_of(L"\\/");
            if (p == std::wstring::npos) {
              wpath.clear();
              break;
            }
            wpath = wpath.substr(0, p);
          }
          if (wpath.empty()) {
            continue;
          }
          const std::wstring wrel(rel.begin(), rel.end());
          const std::wstring wfull = wpath + L'/' + wrel;
          const int nc = WideCharToMultiByte(CP_UTF8, 0, wfull.c_str(), -1, nullptr, 0, nullptr, nullptr);
          if (nc > 0) {
            std::string utf8(static_cast<std::size_t>(nc - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wfull.c_str(), -1, utf8.data(), nc, nullptr, nullptr);
            out.push_back(std::move(utf8));
          }
        }
      }
    }
  }
#endif
}

#ifdef _WIN32
FILE* fopen_utf8_rb_resolve(const char* path_utf8) noexcept {
  const int nw = MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, nullptr, 0);
  if (nw <= 0) {
    return std::fopen(path_utf8, "rb");
  }
  std::vector<wchar_t> w(static_cast<std::size_t>(nw));
  MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, w.data(), nw);
  return _wfopen(w.data(), L"rb");
}

bool path_readable_utf8(const std::string& p) noexcept {
  FILE* f = fopen_utf8_rb_resolve(p.c_str());
  if (f == nullptr) {
    return false;
  }
  std::fclose(f);
  return true;
}
#endif

/// 与资源路径相同：尝试 cwd、../、自 exe 向上各级 + rel，返回第一个能打开的文件路径。
std::string resolve_existing_path(const char* user_path) {
  std::vector<std::string> cands;
  append_path_candidates(std::string(user_path), cands);
  for (const auto& p : cands) {
#ifdef _WIN32
    if (path_readable_utf8(p)) {
      return p;
    }
#else
    std::ifstream f(p, std::ios::binary);
    if (f) {
      return p;
    }
#endif
  }
  return std::string(user_path);
}

bool try_load_icon_texture(const std::string& path, cw::render::Texture2DRgb& out) {
  if (ends_with_icase(path, ".svg")) {
    return cw::render::load_svg_line_icon_texture(path.c_str(), out);
  }
  return false;
}

struct IconTextureCache {
  std::unordered_map<std::string, cw::render::Texture2DRgb> by_logical_path;

  void destroy_all() {
    for (auto& kv : by_logical_path) {
      cw::render::destroy_texture_2d(kv.second);
    }
    by_logical_path.clear();
  }

  cw::render::Texture2DRgb* get_or_load(const std::string& icon_2d_path) {
    const std::string key = icon_2d_path.empty() ? std::string("<default>") : icon_2d_path;
    const auto it = by_logical_path.find(key);
    if (it != by_logical_path.end()) {
      return &it->second;
    }
    cw::render::Texture2DRgb tex{};
    bool ok = false;
    if (!icon_2d_path.empty()) {
      std::vector<std::string> cands;
      append_path_candidates(icon_2d_path, cands);
      for (const auto& p : cands) {
        if (try_load_icon_texture(p, tex)) {
          ok = true;
          break;
        }
      }
    }
    if (!ok) {
      static const char* kDefaultSvg = "assets/icons/AirPlane.svg";
      std::vector<std::string> cands;
      append_path_candidates(kDefaultSvg, cands);
      for (const auto& p : cands) {
        if (try_load_icon_texture(p, tex)) {
          ok = true;
          break;
        }
      }
    }
    if (!tex.valid()) {
      static std::unordered_set<std::string> icon_fail_logged;
      if (icon_fail_logged.insert(key).second) {
        cw::log(cw::LogLevel::Warn,
                std::string("situation_view: icon texture load failed for ").append(key));
      }
      return nullptr;
    }
    by_logical_path[key] = std::move(tex);
    return &by_logical_path[key];
  }
};

void entity_display_rgb(const cw::engine::EntitySituation& e, float& r, float& g, float& b) {
  if (e.has_display_color) {
    r = e.display_color_r;
    g = e.display_color_g;
    b = e.display_color_b;
    return;
  }
  std::string f = e.faction;
  for (char& c : f) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  if (f == "blue") {
    r = 0.25F;
    g = 0.55F;
    b = 1.F;
  } else if (f == "red") {
    r = 1.F;
    g = 0.28F;
    b = 0.25F;
  } else if (f == "green") {
    r = 0.2F;
    g = 0.95F;
    b = 0.35F;
  } else if (f == "yellow" || f == "gold") {
    r = 1.F;
    g = 0.85F;
    b = 0.2F;
  } else if (f == "neutral" || f == "gray" || f == "grey") {
    r = 0.65F;
    g = 0.65F;
    b = 0.7F;
  } else if (f.empty()) {
    r = 0.25F;
    g = 0.95F;
    b = 0.4F;
  } else {
    std::uint32_t h = 2166136261u;
    for (unsigned char c : e.faction) {
      h ^= c;
      h *= 16777619u;
    }
    r = 0.35F + static_cast<float>(h & 0xFFu) * (0.65F / 255.F);
    g = 0.35F + static_cast<float>((h >> 8) & 0xFFu) * (0.65F / 255.F);
    b = 0.35F + static_cast<float>((h >> 16) & 0xFFu) * (0.65F / 255.F);
  }
}

void draw_entities(const cw::engine::Engine& eng, float world_width_m, float world_height_m,
                   int vp_w, int vp_h, float cx_ref, IconTextureCache& icon_cache) {
  // 实体符号：屏幕像素近似固定（不随地图缩放变化）；速度矢量仍按世界尺度绘制。
  const float vpwf = static_cast<float>(std::max(1, vp_w));
  const float vphf = static_cast<float>(std::max(1, vp_h));
  const float m_per_px_x = world_width_m / vpwf;
  const float m_per_px_y = world_height_m / vphf;
  /// 图标在屏幕上的宽度（像素），与参考机标视觉大小相当。
  constexpr float kIconScreenPx = 44.F;
  /// 无纹理时十字「半臂」长度（像素，中心到端点）。
  constexpr float kCrossHalfArmPx = 16.F;
  const float vscale =
      std::max(2.F, std::min(world_width_m * 0.00012F, world_width_m * 0.00003F));
  const auto& snap = eng.situation();

  glDisable(GL_LIGHTING);
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

  for (const auto& e : snap.entities) {
    float cr = 1.F;
    float cg = 1.F;
    float cb = 1.F;
    entity_display_rgb(e, cr, cg, cb);

    cw::render::Texture2DRgb* entity_icon = icon_cache.get_or_load(e.icon_2d_path);
    const bool use_icon = entity_icon != nullptr && entity_icon->valid();
    const float tw = use_icon ? static_cast<float>(entity_icon->width) : 1.F;
    const float th = use_icon ? static_cast<float>(entity_icon->height) : 1.F;
    const float aspect = (tw > 1.F && th > 1.F) ? (th / tw) : 1.F;
    const float hw = 0.5F * kIconScreenPx * m_per_px_x;
    const float hh = 0.5F * kIconScreenPx * aspect * m_per_px_y;
    const float cross_hx = kCrossHalfArmPx * m_per_px_x;
    const float cross_hy = kCrossHalfArmPx * m_per_px_y;

    const float x = cw::render::TacticalMercatorMap::mercator_periodic_x(e.position.x, cx_ref);
    const float y = e.position.y;
    const float vx = e.velocity.x;
    const float vy = e.velocity.y;
    const float vm = std::sqrt(vx * vx + vy * vy);

    if (use_icon) {
      glEnable(GL_TEXTURE_2D);
      glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(entity_icon->gl_name));
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glColor4f(cr, cg, cb, 1.F);
      glPushMatrix();
      glTranslatef(x, y, 0.F);
      if (vm > 0.5F) {
        const float ang_deg =
            std::atan2(vy, vx) * 57.2957795F - 90.F;  // 机头朝上纹理 = 航向
        glRotatef(ang_deg, 0.F, 0.F, 1.F);
      }
      glBegin(GL_QUADS);
      glTexCoord2f(0.F, 0.F);
      glVertex2f(-hw, -hh);
      glTexCoord2f(1.F, 0.F);
      glVertex2f(hw, -hh);
      glTexCoord2f(1.F, 1.F);
      glVertex2f(hw, hh);
      glTexCoord2f(0.F, 1.F);
      glVertex2f(-hw, hh);
      glEnd();
      glPopMatrix();
    } else {
      glDisable(GL_TEXTURE_2D);
      glColor3f(cr, cg, cb);
      glBegin(GL_LINES);
      glVertex2f(x - cross_hx, y);
      glVertex2f(x + cross_hx, y);
      glVertex2f(x, y - cross_hy);
      glVertex2f(x, y + cross_hy);
      glEnd();
    }

    if (vm > 0.5F) {
      glDisable(GL_TEXTURE_2D);
      glColor3f(0.35F, 0.85F, 0.95F);
      glLineWidth(2.F);
      glBegin(GL_LINES);
      glVertex2f(x, y);
      glVertex2f(x + vx * vscale, y + vy * vscale);
      glEnd();
    }
  }

  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
  glColor4f(1.F, 1.F, 1.F, 1.F);
}

struct SituationHud {
  double center_lon_deg = 0.;
  double center_lat_deg = 0.;
  double meters_per_px = 0.;
  float zoom_factor = 1.F;
  /// 鼠标处经纬度（右下角 HUD，客户区像素 → 当前战术视锥墨卡托米）。
  bool has_cursor_lonlat = false;
  double cursor_lon_deg = 0.;
  double cursor_lat_deg = 0.;
  /// 三维地球模式：第二行 HUD 显示相机距离等。
  bool hud_is_globe = false;
  double globe_camera_distance = 3.0;
  /// 视线中心附近近似地面视场宽度（米，粗算）；globe_scale_approx 为假定 96 DPI 下的约 1:N。
  double globe_ground_width_m = 0.;
  double globe_scale_approx = 0.;
};

void lonlat_deg_to_unit_sphere(double lon_deg, double lat_deg, float& x, float& y, float& z) {
  double dx = 0.;
  double dy = 0.;
  double dz = 0.;
  cw::render::lonlat_deg_to_unit_sphere(lon_deg, lat_deg, dx, dy, dz);
  x = static_cast<float>(dx);
  y = static_cast<float>(dy);
  z = static_cast<float>(dz);
}

void draw_globe_sphere(unsigned tex_gl) {
  GLUquadricObj* q = gluNewQuadric();
  if (q == nullptr) {
    return;
  }
  gluQuadricTexture(q, GL_TRUE);
  gluQuadricNormals(q, GLU_SMOOTH);
  gluQuadricOrientation(q, GLU_OUTSIDE);
  if (tex_gl != 0U) {
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(tex_gl));
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glColor3f(1.F, 1.F, 1.F);
  } else {
    glDisable(GL_TEXTURE_2D);
    /// 与二维 `draw_ocean_quad` 同色，避免三维洋面偏亮/偏紫。
    glColor3f(0.078F, 0.227F, 0.463F);
  }
  gluSphere(q, 1.0, 72, 48);
  gluDeleteQuadric(q);
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
  glColor4f(1.F, 1.F, 1.F, 1.F);
}

void draw_routes_globe(const cw::engine::Engine& eng, float cx_ref) {
  glDisable(GL_TEXTURE_2D);
  glLineWidth(1.5F);
  glColor3f(0.55F, 0.55F, 0.6F);
  const float W = cw::render::TacticalMercatorMap::kWorldWidthM;
  constexpr float kR = 1.002F;
  for (const auto& r : eng.routes()) {
    if (r.waypoints.size() < 2) {
      continue;
    }
    glBegin(GL_LINE_STRIP);
    float px = cw::render::TacticalMercatorMap::mercator_periodic_x(r.waypoints[0].x, cx_ref);
    for (std::size_t i = 0; i < r.waypoints.size(); ++i) {
      if (i > 0) {
        float d = r.waypoints[i].x - r.waypoints[i - 1].x;
        d -= std::round(d / W) * W;
        px += d;
      }
      double lon = 0.;
      double lat = 0.;
      mercator_meters_to_lonlat(static_cast<double>(px), static_cast<double>(r.waypoints[i].y), lon,
                                lat);
      float gx = 0.F;
      float gy = 0.F;
      float gz = 0.F;
      lonlat_deg_to_unit_sphere(lon, lat, gx, gy, gz);
      glVertex3f(gx * kR, gy * kR, gz * kR);
    }
    glEnd();
  }
}

void draw_airspaces_globe(const cw::engine::Engine& eng, float cx_ref) {
  glLineWidth(1.F);
  constexpr float kR = 1.003F;
  for (const auto& a : eng.airspaces()) {
    if (a.kind == cw::scenario::AirspaceKind::Box) {
      glColor3f(0.25F, 0.55F, 0.75F);
      glBegin(GL_LINE_LOOP);
      const float xs[] = {cw::render::TacticalMercatorMap::mercator_periodic_x(a.box_min.x, cx_ref),
                          cw::render::TacticalMercatorMap::mercator_periodic_x(a.box_max.x, cx_ref),
                          cw::render::TacticalMercatorMap::mercator_periodic_x(a.box_max.x, cx_ref),
                          cw::render::TacticalMercatorMap::mercator_periodic_x(a.box_min.x, cx_ref)};
      const float ys[] = {a.box_min.y, a.box_min.y, a.box_max.y, a.box_max.y};
      for (int k = 0; k < 4; ++k) {
        double lon = 0.;
        double lat = 0.;
        mercator_meters_to_lonlat(static_cast<double>(xs[k]), static_cast<double>(ys[k]), lon, lat);
        float gx = 0.F;
        float gy = 0.F;
        float gz = 0.F;
        lonlat_deg_to_unit_sphere(lon, lat, gx, gy, gz);
        glVertex3f(gx * kR, gy * kR, gz * kR);
      }
      glEnd();
    } else if (a.polygon.size() >= 2) {
      glColor3f(0.25F, 0.55F, 0.75F);
      glBegin(GL_LINE_LOOP);
      for (const auto& p : a.polygon) {
        double lon = 0.;
        double lat = 0.;
        mercator_meters_to_lonlat(static_cast<double>(cw::render::TacticalMercatorMap::mercator_periodic_x(p.x, cx_ref)),
                                  static_cast<double>(p.y), lon, lat);
        float gx = 0.F;
        float gy = 0.F;
        float gz = 0.F;
        lonlat_deg_to_unit_sphere(lon, lat, gx, gy, gz);
        glVertex3f(gx * kR, gy * kR, gz * kR);
      }
      glEnd();
    }
  }
}

void draw_detections_globe(const cw::engine::Engine& eng,
                           const std::unordered_map<cw::engine::EntityId, cw::math::Vec3>& pos_by_id,
                           float cx_ref) {
  glLineWidth(1.F);
  glColor3f(0.95F, 0.85F, 0.2F);
  glEnable(GL_LINE_STIPPLE);
  glLineStipple(1, 0xAAAA);
  constexpr float kR = 1.004F;
  for (const auto& d : eng.situation().sensor_detections) {
    auto io = pos_by_id.find(d.observer_id);
    auto it = pos_by_id.find(d.target_id);
    if (io == pos_by_id.end() || it == pos_by_id.end()) {
      continue;
    }
    double lo0 = 0.;
    double la0 = 0.;
    double lo1 = 0.;
    double la1 = 0.;
    mercator_meters_to_lonlat(static_cast<double>(cw::render::TacticalMercatorMap::mercator_periodic_x(io->second.x, cx_ref)),
                              static_cast<double>(io->second.y), lo0, la0);
    mercator_meters_to_lonlat(
        static_cast<double>(cw::render::TacticalMercatorMap::mercator_periodic_x(it->second.x, io->second.x)),
        static_cast<double>(it->second.y), lo1, la1);
    float x0 = 0.F;
    float y0 = 0.F;
    float z0 = 0.F;
    float x1 = 0.F;
    float y1 = 0.F;
    float z1 = 0.F;
    lonlat_deg_to_unit_sphere(lo0, la0, x0, y0, z0);
    lonlat_deg_to_unit_sphere(lo1, la1, x1, y1, z1);
    glBegin(GL_LINES);
    glVertex3f(x0 * kR, y0 * kR, z0 * kR);
    glVertex3f(x1 * kR, y1 * kR, z1 * kR);
    glEnd();
  }
  glDisable(GL_LINE_STIPPLE);
}

void draw_entities_globe(const cw::engine::Engine& eng, float cx_ref) {
  glDisable(GL_TEXTURE_2D);
  constexpr float kR = 1.006F;
  const float vscale =
      std::max(8.F, std::min(cw::render::TacticalMercatorMap::kWorldWidthM * 0.00012F,
                             cw::render::TacticalMercatorMap::kWorldWidthM * 0.00003F));
  const auto& snap = eng.situation();
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  for (const auto& e : snap.entities) {
    float cr = 1.F;
    float cg = 1.F;
    float cb = 1.F;
    entity_display_rgb(e, cr, cg, cb);
    double lon = 0.;
    double lat = 0.;
    mercator_meters_to_lonlat(static_cast<double>(cw::render::TacticalMercatorMap::mercator_periodic_x(e.position.x, cx_ref)),
                              static_cast<double>(e.position.y), lon, lat);
    float x = 0.F;
    float y = 0.F;
    float z = 0.F;
    lonlat_deg_to_unit_sphere(lon, lat, x, y, z);
    glPointSize(9.F);
    glColor4f(cr, cg, cb, 1.F);
    glBegin(GL_POINTS);
    glVertex3f(x * kR, y * kR, z * kR);
    glEnd();
    const float vx = e.velocity.x;
    const float vy = e.velocity.y;
    const float vm = std::sqrt(vx * vx + vy * vy);
    if (vm > 0.5F) {
      const double ex_m = static_cast<double>(cw::render::TacticalMercatorMap::mercator_periodic_x(e.position.x, cx_ref)) +
                          static_cast<double>(vx) * static_cast<double>(vscale);
      const double ey_m = static_cast<double>(e.position.y) + static_cast<double>(vy) * static_cast<double>(vscale);
      double lon2 = 0.;
      double lat2 = 0.;
      mercator_meters_to_lonlat(ex_m, ey_m, lon2, lat2);
      float x2 = 0.F;
      float y2 = 0.F;
      float z2 = 0.F;
      lonlat_deg_to_unit_sphere(lon2, lat2, x2, y2, z2);
      glLineWidth(2.F);
      glColor3f(0.35F, 0.85F, 0.95F);
      glBegin(GL_LINES);
      glVertex3f(x * kR, y * kR, z * kR);
      glVertex3f(x2 * kR, y2 * kR, z2 * kR);
      glEnd();
    }
  }
  glColor4f(1.F, 1.F, 1.F, 1.F);
  glDisable(GL_BLEND);
}

void draw_hud_gl_globe_variant(int vp_w, int vp_h, GLuint font_base, const SituationHud& hud,
                               const std::vector<cw::render::GlobeLonLatLabel>* grid_labels) {
  if (font_base == 0 || vp_w < 8 || vp_h < 8) {
    return;
  }
  char line1[192];
  char line2[224];
  const double ab_lon = std::fabs(hud.center_lon_deg);
  const double ab_lat = std::fabs(hud.center_lat_deg);
  std::snprintf(line1, sizeof(line1), "Center lon %.5f %c,  lat %.5f %c", ab_lon,
                hud.center_lon_deg >= 0.0 ? 'E' : 'W', ab_lat,
                hud.center_lat_deg >= 0.0 ? 'N' : 'S');
  std::snprintf(line2, sizeof(line2),
                "3D globe  dist=%.4f  width~%.0fm scale~1:%.0f  (drag arcball, wheel zoom)",
                hud.globe_camera_distance, hud.globe_ground_width_m, hud.globe_scale_approx);

  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0.0, static_cast<double>(vp_w), static_cast<double>(vp_h), 0.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  const int x_left = 4;
  const int y1 = std::max(4, vp_h - 46);
  /// 底行略抬高，避免 12pt 位图字形的下沿被客户区底边裁切。
  const int y2 = std::max(4, vp_h - 28);

  auto draw_ascii = [font_base](const char* s, int x, int y) {
    glColor3f(0.02F, 0.02F, 0.04F);
    glRasterPos2i(x + 1, y + 1);
    glListBase(font_base - 32);
    glCallLists(static_cast<GLsizei>(std::strlen(s)), GL_UNSIGNED_BYTE,
                reinterpret_cast<const GLubyte*>(s));
    glColor3f(0.94F, 0.94F, 0.98F);
    glRasterPos2i(x, y);
    glListBase(font_base - 32);
    glCallLists(static_cast<GLsizei>(std::strlen(s)), GL_UNSIGNED_BYTE,
                reinterpret_cast<const GLubyte*>(s));
  };

  draw_ascii(line1, x_left, y1);
  draw_ascii(line2, x_left, y2);

  if (hud.has_cursor_lonlat) {
    char line3[192];
    const double c_ab_lon = std::fabs(hud.cursor_lon_deg);
    const double c_ab_lat = std::fabs(hud.cursor_lat_deg);
    std::snprintf(line3, sizeof(line3), "Cursor lon %.5f %c,  lat %.5f %c", c_ab_lon,
                  hud.cursor_lon_deg >= 0.0 ? 'E' : 'W', c_ab_lat,
                  hud.cursor_lat_deg >= 0.0 ? 'N' : 'S');
    /// `wglUseFontBitmaps` 12pt 实际字宽常大于 8px；估小会导致右对齐起点过右、右侧被裁切。
    constexpr int kApproxCharPx = 12;
    constexpr int kRightMarginPx = 10;
    const int text_w = static_cast<int>(std::strlen(line3)) * kApproxCharPx;
    const int x_right = std::max(4, vp_w - text_w - kRightMarginPx);
    draw_ascii(line3, x_right, y2);
  }

  if (grid_labels != nullptr) {
#ifdef _WIN32
    ensure_gl_window_pos_2f();
#endif
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    constexpr float kPad = 4.F;
    GLint gl_vp[4]{};
    glGetIntegerv(GL_VIEWPORT, gl_vp);
    const float clip_l = static_cast<float>(gl_vp[0]) + kPad;
    const float clip_b = static_cast<float>(gl_vp[1]) + kPad;
    const float clip_r = static_cast<float>(gl_vp[0] + gl_vp[2]) - kPad;
    const float clip_t = static_cast<float>(gl_vp[1] + gl_vp[3]) - kPad;
    for (const auto& L : *grid_labels) {
      if (L.text[0] == '\0') {
        continue;
      }
      float rx = L.sx + L.ox;
      float ry = L.sy + L.oy;
      /// 注记锚点为 `gluProject` / `glWindowPos` 的整窗坐标；分屏右半幅须按当前 `GL_VIEWPORT` 裁剪。
      if (rx < clip_l || ry < clip_b || rx > clip_r || ry > clip_t) {
        continue;
      }
      float rx2 = rx + 1.F;
      float ry2 = ry + 1.F;
      if (rx2 < clip_l || ry2 < clip_b || rx2 > clip_r || ry2 > clip_t) {
        rx2 = std::clamp(rx2, clip_l, clip_r);
        ry2 = std::clamp(ry2, clip_b, clip_t);
      }
      glPixelZoom(static_cast<GLfloat>(L.pixel_scale), static_cast<GLfloat>(L.pixel_scale));
      glColor4f(0.02F, 0.03F, 0.05F, 0.75F);
#ifdef _WIN32
      if (g_gl_window_pos_2f != nullptr) {
        g_gl_window_pos_2f(rx2, ry2);
      } else {
        glRasterPos2f(rx2, ry2);
      }
#else
      glRasterPos2f(rx2, ry2);
#endif
      glListBase(font_base - 32);
      glCallLists(static_cast<GLsizei>(std::strlen(L.text)), GL_UNSIGNED_BYTE,
                  reinterpret_cast<const GLubyte*>(L.text));
      glColor4f(0.9F, 0.91F, 0.94F, 0.95F);
#ifdef _WIN32
      if (g_gl_window_pos_2f != nullptr) {
        g_gl_window_pos_2f(rx, ry);
      } else {
        glRasterPos2f(rx, ry);
      }
#else
      glRasterPos2f(rx, ry);
#endif
      glListBase(font_base - 32);
      glCallLists(static_cast<GLsizei>(std::strlen(L.text)), GL_UNSIGNED_BYTE,
                  reinterpret_cast<const GLubyte*>(L.text));
      glPixelZoom(1.F, 1.F);
    }
    glDisable(GL_BLEND);
  }

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
}

void draw_frame_globe(const cw::engine::Engine& eng, int vp_w, int vp_h, int cursor_mx, int cursor_my,
                      const cw::render::WorldVectorMerc* world_vec, unsigned world_tex_gl,
                      const cw::render::WorldVectorLines* coastlines,
                      const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                      bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base,
                      bool clear_buffers) {
  (void)icon_cache;

  cw::render::MercatorBounds b{};
  g_tactical_map.expand_bounds_from_engine(eng, b);
  cw::render::MercatorOrthoFrustum tactical{};
  g_tactical_map.compute_interactive_frustum(b, vp_w, vp_h, tactical);
  const float cx_ref = (tactical.l + tactical.r) * 0.5F;

  SituationHud hud{};
  {
    /// 视口中心经纬度在设置 MODELVIEW 后由 `GlobeEarthView::try_pixel_lonlat` 更新。
    hud.center_lon_deg = 0.;
    hud.center_lat_deg = 0.;
    hud.meters_per_px = 0.;
    hud.zoom_factor = g_globe_view.camera().distance;
    hud.hud_is_globe = true;
    hud.globe_camera_distance = static_cast<double>(g_globe_view.camera().distance);
    hud.has_cursor_lonlat = false;
    {
      constexpr double kEarthR = 6378137.0;
      constexpr double kPi = 3.14159265358979323846;
      constexpr double kFovYDeg = 50.0;
      const double h_eye =
          std::max(0.0, static_cast<double>(g_globe_view.camera().distance) - 1.0);
      const double tan_half = std::tan(0.5 * kFovYDeg * (kPi / 180.0));
      hud.globe_ground_width_m = 2.0 * h_eye * tan_half * kEarthR;
      /// 96 DPI：1 px?25.4/96 mm；比例尺分母 N?实地宽度(mm) / 图上宽度(mm)。
      constexpr double kMmPerPx = 25.4 / 96.0;
      const double vw = static_cast<double>(std::max(1, vp_w));
      if (hud.globe_ground_width_m > 1.0 && vw > 0.0) {
        hud.globe_scale_approx =
            (hud.globe_ground_width_m * 1000.0) / (vw * kMmPerPx);
      }
    }
  }

  std::vector<cw::render::GlobeLonLatLabel> globe_grid_labels{};

  if (clear_buffers) {
    glClearColor(0.02F, 0.03F, 0.05F, 1.F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  }
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LEQUAL);

  g_globe_view.setup_projection_and_modelview(vp_w, vp_h);

  {
    double clon = 0.;
    double clat = 0.;
    if (g_globe_view.try_pixel_lonlat(cursor_mx, cursor_my, vp_w, vp_h, clon, clat)) {
      hud.cursor_lon_deg = clon;
      hud.cursor_lat_deg = clat;
      hud.has_cursor_lonlat = true;
    }
  }
  bool globe_viewport_center_valid = false;
  double globe_viewport_center_lon = 0.;
  double globe_viewport_center_lat = 0.;
  {
    const int mxc = std::max(0, vp_w / 2);
    const int myc = std::max(0, vp_h / 2);
    if (g_globe_view.try_pixel_lonlat(mxc, myc, vp_w, vp_h, globe_viewport_center_lon,
                                     globe_viewport_center_lat)) {
      hud.center_lon_deg = globe_viewport_center_lon;
      hud.center_lat_deg = globe_viewport_center_lat;
      globe_viewport_center_valid = true;
    }
  }

  /// GLSL 球面：墨卡托栅格陆块图 / equirect / 纯色洋面；无栅格时再叠球面三角化陆块。
  /// 矢量线略抬升避免与球面深度冲突；半径尽量接近 1 以减少与栅格陆地的视差错位。
  constexpr double kGlobeMapLineR = 1.00035;
  constexpr float kOceanRgb[3] = {0.078F, 0.227F, 0.463F};
  if (cw::render::globe_program_ready()) {
    const unsigned tex =
        (world_tex_gl != 0U && k_show_land_basemap) ? world_tex_gl : 0U;
    if (world_vec != nullptr && world_vec->valid()) {
      cw::render::draw_globe_sphere_glsl(0U, kOceanRgb, g_globe_view.lighting_enabled(), k_show_land_basemap);
      if (k_show_land_basemap && !cw::render::globe_merc_atlas_valid()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0F, 1.0F);
        glColor4f(0.2F, 0.62F, 0.28F, 0.93F);
        world_vec->draw_land_fill_sphere(kGlobeMapLineR);
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_BLEND);
        glColor4f(1.F, 1.F, 1.F, 1.F);
      }
    } else {
      cw::render::draw_globe_sphere_glsl(tex, kOceanRgb, g_globe_view.lighting_enabled(), k_show_land_basemap);
    }
  } else if (world_vec != nullptr && world_vec->valid()) {
    draw_globe_sphere(0U);
    if (k_show_land_basemap) {
      glEnable(GL_BLEND);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(1.0F, 1.0F);
      glColor4f(0.2F, 0.62F, 0.28F, 0.93F);
      world_vec->draw_land_fill_sphere(kGlobeMapLineR);
      glDisable(GL_POLYGON_OFFSET_FILL);
      glDisable(GL_BLEND);
      glColor4f(1.F, 1.F, 1.F, 1.F);
    }
  } else {
    const unsigned tex =
        (world_tex_gl != 0U && k_show_land_basemap) ? world_tex_gl : 0U;
    draw_globe_sphere(tex);
  }

  glDisable(GL_TEXTURE_2D);
  if (boundary_lines != nullptr && boundary_lines->valid()) {
    glLineWidth(1.25F);
    glColor3f(0.38F, 0.33F, 0.27F);
    boundary_lines->draw_on_unit_sphere(kGlobeMapLineR);
  }
  if (coastlines != nullptr && coastlines->valid()) {
    glLineWidth(1.75F);
    glColor3f(0.65F, 0.72F, 0.78F);
    coastlines->draw_on_unit_sphere(kGlobeMapLineR);
  }
  {
    constexpr double kGlobeGridR = 1.00055;
    float gex = 0.F;
    float gey = 0.F;
    float gez = 0.F;
    cw::render::GlobeEarthView::compute_eye(g_globe_view.camera().yaw, g_globe_view.camera().pitch,
                                            g_globe_view.camera().distance, gex, gey, gez);
    cw::render::draw_globe_lonlat_grid(vp_w, vp_h, g_globe_view.camera().distance, g_globe_view.content_R(),
                                       static_cast<double>(gex), static_cast<double>(gey), static_cast<double>(gez),
                                       kGlobeGridR, &globe_grid_labels, globe_viewport_center_valid,
                                       globe_viewport_center_lon, globe_viewport_center_lat);
  }
  glLineWidth(1.F);
  glColor3f(1.F, 1.F, 1.F);

  if (draw_simulation_layers) {
    draw_airspaces_globe(eng, cx_ref);
    draw_routes_globe(eng, cx_ref);
    std::unordered_map<cw::engine::EntityId, cw::math::Vec3> pos_by_id;
    for (const auto& e : eng.situation().entities) {
      pos_by_id[e.id] = e.position;
    }
    draw_detections_globe(eng, pos_by_id, cx_ref);
    draw_entities_globe(eng, cx_ref);
  }

  glDisable(GL_DEPTH_TEST);
  glLineWidth(1.F);

#ifdef _WIN32
  if (hud_font_base != 0) {
    draw_hud_gl_globe_variant(vp_w, vp_h, hud_font_base, hud, &globe_grid_labels);
  }
#endif
  if (hud_out != nullptr) {
    *hud_out = hud;
  }
}

#ifdef _WIN32
GLuint create_hud_bitmap_font_lists(HDC hdc) {
  const GLuint base = glGenLists(96);
  if (base == 0) {
    return 0;
  }
  /// 按当前 DC 的 DPI 换算约 12pt，并用 ClearType，减轻高分辨率下 HUD 发糊。
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
  return base;
}

void draw_hud_gl(int vp_w, int vp_h, GLuint font_base, const SituationHud& hud,
                 const std::vector<cw::render::GlobeLonLatLabel>* grid_labels) {
  if (font_base == 0 || vp_w < 8 || vp_h < 8) {
    return;
  }
  char line1[192];
  char line2[224];
  const double ab_lon = std::fabs(hud.center_lon_deg);
  const double ab_lat = std::fabs(hud.center_lat_deg);
  std::snprintf(line1, sizeof(line1), "Center lon %.5f %c,  lat %.5f %c", ab_lon,
                hud.center_lon_deg >= 0.0 ? 'E' : 'W', ab_lat,
                hud.center_lat_deg >= 0.0 ? 'N' : 'S');
  const double view_w_km = hud.meters_per_px * static_cast<double>(std::max(1, vp_w)) / 1000.0;
  std::snprintf(line2, sizeof(line2),
                "Scale  1 px = %.2f m   View width = %.2f km   Zoom = %.2fx", hud.meters_per_px,
                view_w_km, static_cast<double>(hud.zoom_factor));

  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0.0, static_cast<double>(vp_w), static_cast<double>(vp_h), 0.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  const int x_left = 4;
  const int y1 = std::max(4, vp_h - 46);
  const int y2 = std::max(4, vp_h - 28);

  auto draw_ascii = [font_base](const char* s, int x, int y) {
    glColor3f(0.02F, 0.02F, 0.04F);
    glRasterPos2i(x + 1, y + 1);
    glListBase(font_base - 32);
    glCallLists(static_cast<GLsizei>(std::strlen(s)), GL_UNSIGNED_BYTE,
                reinterpret_cast<const GLubyte*>(s));
    glColor3f(0.94F, 0.94F, 0.98F);
    glRasterPos2i(x, y);
    glListBase(font_base - 32);
    glCallLists(static_cast<GLsizei>(std::strlen(s)), GL_UNSIGNED_BYTE,
                reinterpret_cast<const GLubyte*>(s));
  };

  draw_ascii(line1, x_left, y1);
  draw_ascii(line2, x_left, y2);

  if (hud.has_cursor_lonlat) {
    char line3[192];
    const double c_ab_lon = std::fabs(hud.cursor_lon_deg);
    const double c_ab_lat = std::fabs(hud.cursor_lat_deg);
    std::snprintf(line3, sizeof(line3), "Cursor lon %.5f %c,  lat %.5f %c", c_ab_lon,
                  hud.cursor_lon_deg >= 0.0 ? 'E' : 'W', c_ab_lat,
                  hud.cursor_lat_deg >= 0.0 ? 'N' : 'S');
    constexpr int kApproxCharPx = 12;
    constexpr int kRightMarginPx = 10;
    const int text_w = static_cast<int>(std::strlen(line3)) * kApproxCharPx;
    const int x_right = std::max(4, vp_w - text_w - kRightMarginPx);
    /// 与左侧第二行（Scale）同高，贴近窗口底边。
    draw_ascii(line3, x_right, y2);
  }

  if (grid_labels != nullptr) {
#ifdef _WIN32
    ensure_gl_window_pos_2f();
#endif
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    constexpr float kPad = 4.F;
    GLint gl_vp[4]{};
    glGetIntegerv(GL_VIEWPORT, gl_vp);
    const float clip_l = static_cast<float>(gl_vp[0]) + kPad;
    const float clip_b = static_cast<float>(gl_vp[1]) + kPad;
    const float clip_r = static_cast<float>(gl_vp[0] + gl_vp[2]) - kPad;
    const float clip_t = static_cast<float>(gl_vp[1] + gl_vp[3]) - kPad;
    for (const auto& L : *grid_labels) {
      if (L.text[0] == '\0') {
        continue;
      }
      float rx = L.sx + L.ox;
      float ry = L.sy + L.oy;
      if (rx < clip_l || ry < clip_b || rx > clip_r || ry > clip_t) {
        continue;
      }
      float rx2 = rx + 1.F;
      float ry2 = ry + 1.F;
      if (rx2 < clip_l || ry2 < clip_b || rx2 > clip_r || ry2 > clip_t) {
        rx2 = std::clamp(rx2, clip_l, clip_r);
        ry2 = std::clamp(ry2, clip_b, clip_t);
      }
      glPixelZoom(static_cast<GLfloat>(L.pixel_scale), static_cast<GLfloat>(L.pixel_scale));
      glColor4f(0.02F, 0.03F, 0.05F, 0.75F);
#ifdef _WIN32
      if (g_gl_window_pos_2f != nullptr) {
        g_gl_window_pos_2f(rx2, ry2);
      } else {
        glRasterPos2f(rx2, ry2);
      }
#else
      glRasterPos2f(rx2, ry2);
#endif
      glListBase(font_base - 32);
      glCallLists(static_cast<GLsizei>(std::strlen(L.text)), GL_UNSIGNED_BYTE,
                  reinterpret_cast<const GLubyte*>(L.text));
      glColor4f(0.9F, 0.91F, 0.94F, 0.95F);
#ifdef _WIN32
      if (g_gl_window_pos_2f != nullptr) {
        g_gl_window_pos_2f(rx, ry);
      } else {
        glRasterPos2f(rx, ry);
      }
#else
      glRasterPos2f(rx, ry);
#endif
      glListBase(font_base - 32);
      glCallLists(static_cast<GLsizei>(std::strlen(L.text)), GL_UNSIGNED_BYTE,
                  reinterpret_cast<const GLubyte*>(L.text));
      glPixelZoom(1.F, 1.F);
    }
    glDisable(GL_BLEND);
  }

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
}
#endif

void draw_split_divider(int vp_w, int vp_h, int split_x) {
  if (vp_w < 4 || vp_h < 4 || split_x <= 0 || split_x >= vp_w) {
    return;
  }
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_TEXTURE_2D);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0.0, static_cast<double>(vp_w), static_cast<double>(vp_h), 0.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();
  glColor3f(0.48F, 0.5F, 0.54F);
  glLineWidth(2.F);
  glBegin(GL_LINES);
  {
    const double x = static_cast<double>(split_x) + 0.5;
    glVertex2d(x, 0.0);
    glVertex2d(x, static_cast<double>(vp_h));
  }
  glEnd();
  glLineWidth(1.F);
  glColor3f(1.F, 1.F, 1.F);
  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
}

void draw_frame_tactical(const cw::engine::Engine& eng, int vp_w, int vp_h, int cursor_mx, int cursor_my,
                         const cw::render::WorldVectorMerc* world_vec, unsigned world_tex_gl,
                         const cw::render::WorldVectorLines* coastlines,
                         const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                         bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base,
                         bool clear_color_buffer) {
  cw::render::MercatorBounds b{};
  g_tactical_map.expand_bounds_from_engine(eng, b);
  cw::render::MercatorOrthoFrustum tactical{};
  g_tactical_map.compute_interactive_frustum(b, vp_w, vp_h, tactical);
  cw::render::TacticalMercatorMap::apply_ortho_frustum(tactical);

  const float cx_ref = (tactical.l + tactical.r) * 0.5F;

  bool tactical_vp_center_valid = false;
  double tactical_vp_center_lon = 0.;
  double tactical_vp_center_lat = 0.;
  std::vector<cw::render::GlobeLonLatLabel> tactical_grid_labels{};

  SituationHud hud{};
  {
    const double cx = static_cast<double>(cx_ref);
    const double cy = static_cast<double>(tactical.b + tactical.t) * 0.5;
    mercator_meters_to_lonlat(cx, cy, hud.center_lon_deg, hud.center_lat_deg);
    hud.meters_per_px =
        static_cast<double>(tactical.r - tactical.l) / static_cast<double>(std::max(1, vp_w));
    hud.zoom_factor = g_tactical_map.zoom();
    if (vp_w >= 1 && vp_h >= 1) {
      cw::render::TacticalMercatorMap::window_pixel_to_lonlat(cursor_mx, cursor_my, vp_w, vp_h, tactical,
                                                                hud.cursor_lon_deg, hud.cursor_lat_deg);
      hud.has_cursor_lonlat = true;
      cw::render::TacticalMercatorMap::window_pixel_to_lonlat(vp_w / 2, vp_h / 2, vp_w, vp_h, tactical,
                                                              tactical_vp_center_lon, tactical_vp_center_lat);
      tactical_vp_center_valid = true;
    }
  }
  if (hud_out != nullptr) {
    *hud_out = hud;
  }

  if (clear_color_buffer) {
    glClearColor(0.02F, 0.03F, 0.05F, 1.F);
    glClear(GL_COLOR_BUFFER_BIT);
  }

  glDisable(GL_DEPTH_TEST);

  cw::render::MercatorOrthoFrustum basemap{};
  cw::render::TacticalMercatorMap::expand_frustum_for_world_basemap(tactical, basemap);
  if (world_vec != nullptr && world_vec->valid()) {
    draw_world_underlay_vector(basemap, world_vec);
  } else if (world_tex_gl != 0 && k_show_land_basemap) {
    draw_world_underlay(basemap, world_tex_gl);
  } else {
    draw_ocean_quad(basemap);
  }

  {
    const float ww = tactical.r - tactical.l;
    if (boundary_lines != nullptr && boundary_lines->valid()) {
      glLineWidth(std::clamp(ww / 520000.F, 0.4F, 2.2F));
      glColor3f(0.38F, 0.33F, 0.27F);
      draw_world_vector_lines_tiled(boundary_lines, tactical.l, tactical.r);
    }
    if (coastlines != nullptr && coastlines->valid()) {
      glLineWidth(std::clamp(ww / 200000.F, 1.F, 4.5F));
      glColor3f(0.68F, 0.76F, 0.82F);
      draw_world_vector_lines_tiled(coastlines, tactical.l, tactical.r);
    }
  }
  glLineWidth(1.F);

  cw::render::draw_tactical_lonlat_grid(vp_w, vp_h, tactical, cx_ref, &tactical_grid_labels,
                                        tactical_vp_center_valid, tactical_vp_center_lon,
                                        tactical_vp_center_lat);

  if (draw_simulation_layers) {
    draw_airspaces(eng, cx_ref);
    {
      const float ww = tactical.r - tactical.l;
      glLineWidth(std::clamp(ww / 200000.F, 1.F, 6.F));
    }
    draw_routes(eng, cx_ref);

    std::unordered_map<cw::engine::EntityId, cw::math::Vec3> pos_by_id;
    for (const auto& e : eng.situation().entities) {
      pos_by_id[e.id] = e.position;
    }
    draw_detections(eng, pos_by_id, cx_ref);
    draw_entities(eng, tactical.r - tactical.l, tactical.t - tactical.b, vp_w, vp_h, cx_ref,
                  icon_cache);
  }

  glLineWidth(1.F);

#ifdef _WIN32
  if (hud_font_base != 0) {
    draw_hud_gl(vp_w, vp_h, hud_font_base, hud, &tactical_grid_labels);
  }
#endif
}

void draw_frame_split(const cw::engine::Engine& eng, int vp_w, int vp_h, int cursor_mx, int cursor_my,
                      const cw::render::WorldVectorMerc* world_vec, unsigned world_tex_gl,
                      const cw::render::WorldVectorLines* coastlines,
                      const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                      bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base) {
  const int split_x = std::max(1, vp_w / 2);
  const int right_w = std::max(1, vp_w - split_x);
  const int t_mx =
      (cursor_mx >= 0 && cursor_mx < split_x) ? cursor_mx : std::clamp(split_x / 2, 0, split_x - 1);
  const int t_my = cursor_my;
  const int g_mx = (cursor_mx >= split_x) ? (cursor_mx - split_x) : std::clamp(right_w / 2, 0, right_w - 1);
  const int g_my = cursor_my;

  glViewport(0, 0, vp_w, vp_h);
  glClearColor(0.02F, 0.03F, 0.05F, 1.F);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glViewport(0, 0, split_x, vp_h);
  draw_frame_tactical(eng, split_x, vp_h, t_mx, t_my, world_vec, world_tex_gl, coastlines, boundary_lines,
                      icon_cache, draw_simulation_layers, hud_out, hud_font_base, false);

  glViewport(split_x, 0, right_w, vp_h);
  draw_frame_globe(eng, right_w, vp_h, g_mx, g_my, world_vec, world_tex_gl, coastlines, boundary_lines,
                   icon_cache, draw_simulation_layers, hud_out, hud_font_base, false);

  glViewport(0, 0, vp_w, vp_h);
  draw_split_divider(vp_w, vp_h, split_x);
}

void draw_frame(const cw::engine::Engine& eng, int vp_w, int vp_h, int cursor_mx, int cursor_my,
                const cw::render::WorldVectorMerc* world_vec, unsigned world_tex_gl,
                const cw::render::WorldVectorLines* coastlines,
                const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base,
                ViewMode view_mode) {
  if (view_mode == ViewMode::Tactical2D) {
    reset_globe_view_auxiliary_state();
  }
  if (view_mode == ViewMode::Globe3d) {
    draw_frame_globe(eng, vp_w, vp_h, cursor_mx, cursor_my, world_vec, world_tex_gl, coastlines,
                      boundary_lines, icon_cache, draw_simulation_layers, hud_out, hud_font_base, true);
    return;
  }
  if (view_mode == ViewMode::Split2dGlobe) {
    draw_frame_split(eng, vp_w, vp_h, cursor_mx, cursor_my, world_vec, world_tex_gl, coastlines,
                      boundary_lines, icon_cache, draw_simulation_layers, hud_out, hud_font_base);
    return;
  }
  draw_frame_tactical(eng, vp_w, vp_h, cursor_mx, cursor_my, world_vec, world_tex_gl, coastlines,
                      boundary_lines, icon_cache, draw_simulation_layers, hud_out, hud_font_base, true);
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  const bool map_only = argc < 2 || argv[1] == nullptr || argv[1][0] == '\0';

  cw::scenario::Scenario sc{};
  if (map_only) {
    sc.version = 2;
  } else {
    const std::string scen_path = resolve_existing_path(argv[1]);
    g_scenario_file_dir = scenario_file_parent_absolute_utf8(scen_path);
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

  g_hwnd_main = static_cast<HWND>(win.win32_hwnd());
  {
    HMENU h_bar = CreateMenu();
    HMENU h_view = CreateMenu();
    g_hmenu_view = h_view;
    AppendMenuW(h_view, MF_STRING | MF_CHECKED, static_cast<UINT_PTR>(kMenuView2d), L"2D tactical map");
    AppendMenuW(h_view, MF_STRING, static_cast<UINT_PTR>(kMenuView3d), L"3D globe");
    AppendMenuW(h_view, MF_STRING, static_cast<UINT_PTR>(kMenuViewSplit2d3d), L"2D + 3D split view");
    AppendMenuW(h_view, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(h_view, MF_STRING, static_cast<UINT_PTR>(kMenuGlobeLighting), L"三维地图光照");
    AppendMenuW(h_bar, MF_POPUP, reinterpret_cast<UINT_PTR>(h_view), L"View");
    SetMenu(g_hwnd_main, h_bar);
    DrawMenuBar(g_hwnd_main);
  }
  win.set_menu_command_callback(on_view_menu, nullptr);

  GLuint hud_font_base = 0;
  {
    HDC hdc = static_cast<HDC>(win.win32_hdc());
    hud_font_base = create_hud_bitmap_font_lists(hdc);
    if (hud_font_base == 0) {
      cw::log(cw::LogLevel::Info, "situation_view: bitmap font for HUD unavailable");
    }
  }

  win.make_current();
  if (!cw::render::globe_program_try_init()) {
    cw::log(cw::LogLevel::Info, "situation_view: GLSL globe not available (fallback to gluSphere)");
  }

  cw::render::WorldVectorMerc world_vec{};
  const char* const kVectorCandidates[] = {
      "assets/maps/world_land.merc2",
      "../assets/maps/world_land.merc2",
      "../../assets/maps/world_land.merc2",
  };
  const char* vector_loaded_from = nullptr;
  for (const char* p : kVectorCandidates) {
    if (world_vec.load_from_file(p)) {
      vector_loaded_from = p;
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

  if (k_show_land_basemap && vector_loaded_from != nullptr && cw::render::globe_program_ready()) {
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
    for (const char* p : kMapCandidates) {
      if (cw::render::load_texture_bmp_rgb24(p, world_tex)) {
        map_loaded_from = p;
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

  IconTextureCache entity_icons{};

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
  for (const char* p : kCoastCandidates) {
    if (coastlines.load_from_file(p)) {
      coast_loaded_from = p;
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
  for (const char* p : kBoundaryCandidates) {
    if (boundary_lines.load_from_file(p)) {
      boundary_loaded_from = p;
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
    if (win.left_button_down()) {
      if (g_drag_prev_valid) {
        const int split_x = std::max(1, cw / 2);
        const int mx = win.mouse_client_x();
        /// 分屏时以**上一帧**光标所在半幅判定拖动归属，避免扫过中缝时来回切换。
        const bool split_drag_left =
            (g_view_mode != ViewMode::Split2dGlobe) || (g_drag_prev_mx < split_x);
        const bool split_drag_right =
            (g_view_mode == ViewMode::Split2dGlobe) && !split_drag_left;
        if (g_view_mode == ViewMode::Tactical2D || split_drag_left) {
          const int dx = mx - g_drag_prev_mx;
          const int dy = win.mouse_client_y() - g_drag_prev_my;
          const int pan_w = (g_view_mode == ViewMode::Split2dGlobe) ? split_x : cw;
          g_tactical_map.apply_mouse_pan_drag(engine, pan_w, ch, dx, dy);
          if (g_view_mode == ViewMode::Split2dGlobe && (dx != 0 || dy != 0)) {
            split_left_driven = true;
          }
        } else if (g_view_mode == ViewMode::Globe3d || split_drag_right) {
          /// 三维：弧球拖动；分屏时坐标相对于右半幅视口。
          const int pmx = g_drag_prev_mx;
          const int pmy = g_drag_prev_my;
          const int cmx = mx;
          const int cmy = win.mouse_client_y();
          if (pmx != cmx || pmy != cmy) {
            if (g_view_mode == ViewMode::Split2dGlobe) {
              g_globe_view.queue_arcball_drag(pmx - split_x, pmy, cmx - split_x, cmy);
            } else {
              g_globe_view.queue_arcball_drag(pmx, pmy, cmx, cmy);
            }
            if (g_view_mode == ViewMode::Split2dGlobe) {
              split_right_driven = true;
            }
          }
        }
      }
      g_drag_prev_mx = win.mouse_client_x();
      g_drag_prev_my = win.mouse_client_y();
      g_drag_prev_valid = true;
    } else {
      g_drag_prev_valid = false;
      g_globe_view.clear_arcball_pending();
    }

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

    {
      const int wd = win.consume_wheel_delta();
      if (wd != 0) {
        const int split_x = std::max(1, cw / 2);
        const int mx = win.mouse_client_x();
        if (g_view_mode == ViewMode::Split2dGlobe) {
          if (mx < split_x) {
            split_left_driven = true;
          } else {
            split_right_driven = true;
          }
        }
        const bool wheel_on_globe =
            (g_view_mode == ViewMode::Globe3d) ||
            (g_view_mode == ViewMode::Split2dGlobe && mx >= split_x);
        if (wheel_on_globe) {
          /// 在 **h=distance-1** 上乘除步进，而不是在 distance 上乘除：否则贴地时一格滚轮会把 h 加上约
          /// (kStep-1)（~0.02），比例尺会从 ~1:300 跳到 ~1:3e5。离地越近 kStep 越接近 1，末端更细腻。
          constexpr float kGlobeDistMin = 1.00002F;
          constexpr float kGlobeDistMax = 28.F;
          constexpr float kWheelHRef = 2.2F;
          constexpr float kStepFar = 1.2F;
          constexpr float kStepNear = 1.1F;
          const float h = std::max(g_globe_view.camera().distance - 1.0F, 1.0e-8F);
          const float t = std::clamp(h / kWheelHRef, 0.0F, 1.0F);
          const float kStep = kStepNear + (kStepFar - kStepNear) * t;
          const float h_new = (wd > 0) ? (h / kStep) : (h * kStep);
          const float dist = 1.0F + h_new;
          g_globe_view.camera().distance = std::clamp(dist, kGlobeDistMin, kGlobeDistMax);
        } else {
          g_tactical_map.apply_wheel_zoom(wd);
        }
      }
    }

#ifdef _WIN32
    win.make_current();
#endif
    glViewport(0, 0, cw, ch);

    if (g_view_mode == ViewMode::Split2dGlobe) {
      const int split_x = std::max(1, cw / 2);
      const int right_w = std::max(1, cw - split_x);
      if (g_globe_view.arcball_pending()) {
        glViewport(split_x, 0, right_w, ch);
        g_globe_view.setup_projection_and_modelview(right_w, ch);
        glViewport(0, 0, cw, ch);
      }
      cw::render::MercatorBounds b{};
      g_tactical_map.expand_bounds_from_engine(engine, b);
      cw::render::MercatorOrthoFrustum tact{};
      g_tactical_map.compute_interactive_frustum(b, split_x, ch, tact);
      const double tcx = static_cast<double>((tact.l + tact.r) * 0.5F);
      const double tcy = static_cast<double>((tact.b + tact.t) * 0.5F);
      double t_lon = 0.;
      double t_lat = 0.;
      mercator_meters_to_lonlat(tcx, tcy, t_lon, t_lat);
      double g_lon = 0.;
      double g_lat = 0.;
      g_globe_view.viewport_center_lonlat_from_pose(g_lon, g_lat);
      constexpr double kPi = 3.14159265358979323846;
      auto tactical_center_ew_m = [](const cw::render::MercatorOrthoFrustum& t, double lat_deg) -> double {
        const double lat_r = lat_deg * (kPi / 180.0);
        return static_cast<double>(t.r - t.l) * std::max(1e-4, std::cos(lat_r));
      };
      auto sync_scale_left_to_right = [&]() {
        const double ew = tactical_center_ew_m(tact, t_lat);
        if (ew > 1.0) {
          g_globe_view.set_camera_distance_for_visible_ew_meters(right_w, ch, ew);
        }
      };
      auto sync_scale_right_to_left = [&]() {
        const double ew = g_globe_view.visible_ground_ew_meters(right_w, ch);
        if (ew > 1.0) {
          g_tactical_map.set_visible_ground_ew_meters_at_lat(engine, split_x, ch, ew, g_lat);
        }
      };

      if (g_split_initial_sync_pending) {
        g_globe_view.orient_content_to_place_lonlat_at_screen_center(t_lon, t_lat);
        g_split_initial_sync_pending = false;
        sync_scale_left_to_right();
      } else if (split_left_driven || split_right_driven) {
        if (split_left_driven && !split_right_driven) {
          g_globe_view.orient_content_to_place_lonlat_at_screen_center(t_lon, t_lat);
          sync_scale_left_to_right();
        } else if (split_right_driven && !split_left_driven) {
          g_tactical_map.set_frustum_center_lonlat(engine, split_x, ch, g_lon, g_lat);
          sync_scale_right_to_left();
        } else {
          g_globe_view.orient_content_to_place_lonlat_at_screen_center(t_lon, t_lat);
          sync_scale_left_to_right();
        }
      }
    }

    draw_frame(engine, cw, ch, win.mouse_client_x(), win.mouse_client_y(),
               world_vec.valid() ? &world_vec : nullptr,
               world_tex.valid() ? world_tex.gl_name : 0U,
               coastlines.valid() ? &coastlines : nullptr,
               boundary_lines.valid() ? &boundary_lines : nullptr, entity_icons, !map_only,
               nullptr, hud_font_base, g_view_mode);
    win.swap_buffers();

#ifdef _WIN32
    if ((GetAsyncKeyState(VK_HOME) & 0x1) != 0) {
      reset_view_camera();
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
