#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "cw/situation_view/situation_map_globe_render.hpp"

#include "cw/ecs/entity_coordinate_system.hpp"
#include "cw/engine/situation.hpp"
#include "cw/engine/types.hpp"
#include "cw/scenario/scenario.hpp"
#include "cw/render/globe_program.hpp"
#include "cw/render/globe_pixel_scale.hpp"
#include "cw/render/globe_view_3d.hpp"
#include "cw/render/lonlat_grid.hpp"
#include "cw/render/mercator_geo.hpp"
#include "cw/render/tactical_map_2d.hpp"
#include "cw/render/texture_bmp.hpp"
#include "cw/render/world_vector_merc.hpp"
#include "cw/render/world_vector_lines.hpp"

#include <GL/glu.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <unordered_map>
#include <vector>

namespace cw::situation_view {

using cw::render::mercator_meters_to_lonlat;

namespace {

void route_line_rgb(const cw::scenario::ScenarioRoute& r, float& out_r, float& out_g, float& out_b) {
  if (r.has_line_color) {
    out_r = r.line_r;
    out_g = r.line_g;
    out_b = r.line_b;
  } else {
    out_r = 0.55F;
    out_g = 0.55F;
    out_b = 0.6F;
  }
}

float route_line_width_px(const cw::scenario::ScenarioRoute& r) {
  return r.has_line_width ? r.line_width_px : 1.5F;
}

}  // namespace

#ifdef _WIN32
using GlWindowPos2fFn = void(APIENTRY*)(GLfloat, GLfloat);
static GlWindowPos2fFn g_gl_window_pos_2f = nullptr;

static void ensure_gl_window_pos_2f() {
  if (g_gl_window_pos_2f == nullptr) {
    auto* p = wglGetProcAddress("glWindowPos2f");
    g_gl_window_pos_2f = reinterpret_cast<GlWindowPos2fFn>(reinterpret_cast<void*>(p));
  }
}
#endif

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
                                const cw::render::WorldVectorMerc* wv, bool show_land) {
  draw_ocean_quad(f);
  if (show_land && wv != nullptr && wv->valid()) {
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

void draw_airspaces(const cw::engine::SituationPresentation& world, float cx_ref) {
  glLineWidth(1.F);
  for (const auto& a : world.airspaces) {
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

void draw_routes(const cw::engine::SituationPresentation& world, float cx_ref, float world_width_m, float world_height_m,
                   int vp_w, int vp_h, GLuint waypoint_font_base) {
  const float W = cw::render::TacticalMercatorMap::kWorldWidthM;
  const float vpwf = static_cast<float>(std::max(1, vp_w));
  const float vphf = static_cast<float>(std::max(1, vp_h));
  const float m_per_px_x = world_width_m / vpwf;
  const float m_per_px_y = world_height_m / vphf;
  /// 与实体机标类似的屏幕像素直径，航线点圆点略小。
  constexpr float kWaypointDotPx = 10.F;
  const float rad = 0.5F * kWaypointDotPx * std::max(m_per_px_x, m_per_px_y);
  constexpr int kCircleSeg = 20;
  constexpr float kTwoPi = 6.2831855F;

  for (const auto& r : world.routes) {
    if (r.waypoints.size() < 2) {
      continue;
    }
    float cr = 0.F;
    float cg = 0.F;
    float cb = 0.F;
    route_line_rgb(r, cr, cg, cb);
    glLineWidth(route_line_width_px(r));
    glColor3f(cr, cg, cb);
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

    glDisable(GL_TEXTURE_2D);
    glColor4f(cr * 1.05F, cg * 1.05F, cb * 1.05F, 1.F);
    px = cw::render::TacticalMercatorMap::mercator_periodic_x(r.waypoints[0].x, cx_ref);
    for (std::size_t i = 0; i < r.waypoints.size(); ++i) {
      if (i > 0) {
        float d = r.waypoints[i].x - r.waypoints[i - 1].x;
        d -= std::round(d / W) * W;
        px += d;
      }
      const float py = r.waypoints[i].y;
      glBegin(GL_TRIANGLE_FAN);
      glVertex2f(px, py);
      for (int s = 0; s <= kCircleSeg; ++s) {
        const float t = (static_cast<float>(s) / static_cast<float>(kCircleSeg)) * kTwoPi;
        glVertex2f(px + rad * std::cos(t), py + rad * std::sin(t));
      }
      glEnd();
    }

    if (waypoint_font_base != 0U) {
      glColor3f(0.94F, 0.95F, 0.88F);
      px = cw::render::TacticalMercatorMap::mercator_periodic_x(r.waypoints[0].x, cx_ref);
      for (std::size_t i = 0; i < r.waypoints.size(); ++i) {
        if (i > 0) {
          float d = r.waypoints[i].x - r.waypoints[i - 1].x;
          d -= std::round(d / W) * W;
          px += d;
        }
        const float py = r.waypoints[i].y;
        char lab[16];
        std::snprintf(lab, sizeof(lab), "%u", static_cast<unsigned>(i));
        glColor3f(0.02F, 0.02F, 0.04F);
        glRasterPos2f(px + rad * 1.35F + 1.F, py + rad * 0.25F + 1.F);
        glListBase(waypoint_font_base - 32);
        glCallLists(static_cast<GLsizei>(std::strlen(lab)), GL_UNSIGNED_BYTE,
                    reinterpret_cast<const GLubyte*>(lab));
        glColor3f(0.94F, 0.95F, 0.88F);
        glRasterPos2f(px + rad * 1.35F, py + rad * 0.25F);
        glListBase(waypoint_font_base - 32);
        glCallLists(static_cast<GLsizei>(std::strlen(lab)), GL_UNSIGNED_BYTE,
                    reinterpret_cast<const GLubyte*>(lab));
      }
    }
    glColor4f(1.F, 1.F, 1.F, 1.F);
  }
}

void draw_detections(const cw::engine::SituationPresentation& world,
                     const std::unordered_map<cw::engine::EntityId, cw::math::Vec3>& pos_by_id,
                     float cx_ref) {
  glLineWidth(1.F);
  glColor3f(0.95F, 0.85F, 0.2F);
  glEnable(GL_LINE_STIPPLE);
  glLineStipple(1, 0xAAAA);
  for (const auto& d : world.situation.sensor_detections) {
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

void draw_entities(const cw::engine::SituationPresentation& world, float world_width_m, float world_height_m,
                   int vp_w, int vp_h, float cx_ref, IconTextureCache& icon_cache) {
  // 实体符号：屏幕像素近似固定（不随地图缩放变化）。速度矢量用世界尺度 `vscale`，但放大后 vscale 很小，
  // 矢量在世界米下短于亚像素，故对屏幕长度设下限；三维地球侧用同一套 `vscale` 与最小像素长。
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
  const auto& snap = world.situation;

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
      /// 机头方向由 `entity_att`（态势中的 yaw/pitch/roll）经 ECS 变换；与速度矢量解耦。
      {
        const float ang_deg =
            cw::ecs::EntityCoordinateSystem::icon_rotation_deg_mercator(e.yaw_deg, e.pitch_deg, e.roll_deg);
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
      float dx = vx * vscale;
      float dy = vy * vscale;
      constexpr float kMinSpeedVecPx = 32.F;
      const float px_len = std::hypot(dx / m_per_px_x, dy / m_per_px_y);
      if (px_len < kMinSpeedVecPx && px_len > 1e-6f) {
        const float t = kMinSpeedVecPx / px_len;
        dx *= t;
        dy *= t;
      }
      glBegin(GL_LINES);
      glVertex2f(x, y);
      glVertex2f(x + dx, y + dy);
      glEnd();
    }
  }

  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
  glColor4f(1.F, 1.F, 1.F, 1.F);
}



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

/// OpenGL 地球模型半径为 1（海平面）；`alt_m` 为海拔米（与想定 `box`/`ap_vert` 的 z 一致）。
/// 须略高于 `draw_globe_lonlat_grid` 使用的 `kGlobeGridR`（1.00055），否则空域线会被经纬网盖住；
/// 亦勿回到 ~1.003，否则海平面空域会「悬空」约 19 km。
static float globe_radius_for_airspace_alt_m(float alt_m_min) {
  constexpr float kEarthR_m = 6378137.f;
  /// 与 `globe_view_3d.cpp` 中 `draw_globe_lonlat_grid` 的球面半径一致。
  constexpr float kGlobeGridR = 1.00055f;
  const float r = 1.0f + std::max(0.f, alt_m_min) / kEarthR_m;
  return std::max(kGlobeGridR + 0.00012f, r + 0.00012f);
}

void draw_routes_globe(const cw::engine::SituationPresentation& world, float cx_ref, int vp_w, int vp_h,
                       std::vector<cw::render::GlobeLonLatLabel>* labels_out, float camera_distance) {
  glDisable(GL_TEXTURE_2D);
  const float W = cw::render::TacticalMercatorMap::kWorldWidthM;
  const float ref_d = 3.2F;
  float pixel_scale = ref_d / std::max(1.001F, camera_distance);
  pixel_scale *= std::sqrt(static_cast<float>(std::max(360, vp_h)) / 720.F);
  pixel_scale = cw::render::clamp_globe_label_pixel_scale(pixel_scale);

  for (const auto& r : world.routes) {
    if (r.waypoints.size() < 2) {
      continue;
    }
    float cr = 0.F;
    float cg = 0.F;
    float cb = 0.F;
    route_line_rgb(r, cr, cg, cb);
    glLineWidth(route_line_width_px(r));
    glColor3f(cr, cg, cb);
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
      const float kR = globe_radius_for_airspace_alt_m(r.waypoints[i].z);
      glVertex3f(gx * kR, gy * kR, gz * kR);
    }
    glEnd();

    glPointSize(8.F);
    glColor4f(cr * 1.08F, cg * 1.08F, cb * 1.08F, 1.F);
    glBegin(GL_POINTS);
    px = cw::render::TacticalMercatorMap::mercator_periodic_x(r.waypoints[0].x, cx_ref);
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
      const float kRp = globe_radius_for_airspace_alt_m(r.waypoints[i].z);
      glVertex3f(gx * kRp, gy * kRp, gz * kRp);
    }
    glEnd();
    glPointSize(1.F);
    glColor4f(1.F, 1.F, 1.F, 1.F);

    if (labels_out != nullptr) {
      px = cw::render::TacticalMercatorMap::mercator_periodic_x(r.waypoints[0].x, cx_ref);
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
        const float kRl = globe_radius_for_airspace_alt_m(r.waypoints[i].z);
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%u", static_cast<unsigned>(i));
        cw::render::append_lonlat_grid_label(labels_out, vp_w, vp_h, pixel_scale,
                                             static_cast<double>(gx * kRl), static_cast<double>(gy * kRl),
                                             static_cast<double>(gz * kRl), 5.F, 5.F, buf);
      }
    }
  }
}

void draw_airspaces_globe(const cw::engine::SituationPresentation& world, float cx_ref) {
  glLineWidth(1.F);
  for (const auto& a : world.airspaces) {
    if (a.kind == cw::scenario::AirspaceKind::Box) {
      const float alt_floor = std::min(a.box_min.z, a.box_max.z);
      const float kR = globe_radius_for_airspace_alt_m(alt_floor);
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
      float alt_floor = a.polygon[0].z;
      for (const auto& p : a.polygon) {
        alt_floor = std::min(alt_floor, p.z);
      }
      const float kR = globe_radius_for_airspace_alt_m(alt_floor);
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

void draw_detections_globe(const cw::engine::SituationPresentation& world,
                           const std::unordered_map<cw::engine::EntityId, cw::math::Vec3>& pos_by_id,
                           float cx_ref) {
  glLineWidth(1.F);
  glColor3f(0.95F, 0.85F, 0.2F);
  glEnable(GL_LINE_STIPPLE);
  glLineStipple(1, 0xAAAA);
  constexpr float kR = 1.004F;
  for (const auto& d : world.situation.sensor_detections) {
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

void draw_entities_globe(const cw::engine::SituationPresentation& world, float cx_ref,
                         const cw::render::GlobeEarthView& globe, int vp_w, int vp_h) {
  glDisable(GL_TEXTURE_2D);
  const float vpwf = static_cast<float>(std::max(1, vp_w));
  const float vphf = static_cast<float>(std::max(1, vp_h));
  const double ew_d = globe.visible_ground_ew_meters(vp_w, vp_h);
  const float world_width_m = static_cast<float>(std::max(ew_d, 1.0));
  const float world_height_m = world_width_m * vphf / vpwf;
  const float vscale =
      std::max(2.F, std::min(world_width_m * 0.00012F, world_width_m * 0.00003F));
  constexpr float kMinSpeedVecPx = 32.F;
  const float m_per_px_x = world_width_m / vpwf;
  const float m_per_px_y = world_height_m / vphf;
  const auto& snap = world.situation;
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
    const float kR = globe_radius_for_airspace_alt_m(e.position.z);
    glPointSize(9.F);
    glColor4f(cr, cg, cb, 1.F);
    glBegin(GL_POINTS);
    glVertex3f(x * kR, y * kR, z * kR);
    glEnd();
    const float vx = e.velocity.x;
    const float vy = e.velocity.y;
    const float vm = std::sqrt(vx * vx + vy * vy);
    if (vm > 0.5F) {
      float dx = vx * vscale;
      float dy = vy * vscale;
      const float px_len = std::hypot(dx / m_per_px_x, dy / m_per_px_y);
      if (px_len < kMinSpeedVecPx && px_len > 1e-6F) {
        const float t = kMinSpeedVecPx / px_len;
        dx *= t;
        dy *= t;
      }
      const double ex_m = static_cast<double>(cw::render::TacticalMercatorMap::mercator_periodic_x(e.position.x, cx_ref)) +
                          static_cast<double>(dx);
      const double ey_m = static_cast<double>(e.position.y) + static_cast<double>(dy);
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
                "3D globe  dist=%.4f  EW~%.0fm scale~1:%.0f  (drag arcball, wheel zoom)",
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

void draw_frame_globe(const cw::engine::SituationPresentation& world, SituationViewShell& shell, int vp_w,
                      int vp_h, int tactical_frustum_vp_w, int cursor_mx, int cursor_my,
                      const cw::render::WorldVectorMerc* world_vec, unsigned world_tex_gl,
                      const cw::render::WorldVectorLines* coastlines,
                      const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                      bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base,
                      bool clear_buffers, const SituationRenderOptions& opts) {
  (void)icon_cache;

  cw::render::MercatorBounds b{};
  shell.tactical_map().expand_bounds_from_presentation(world, b);
  cw::render::MercatorOrthoFrustum tactical{};
  const int tact_w = std::max(1, tactical_frustum_vp_w);
  shell.tactical_map().compute_interactive_frustum(b, tact_w, vp_h, tactical);
  const float cx_ref = (tactical.l + tactical.r) * 0.5F;

  SituationHud hud{};
  {
    /// 视口中心经纬度在设置 MODELVIEW 后由 `GlobeEarthView::try_pixel_lonlat` 更新。
    hud.center_lon_deg = 0.;
    hud.center_lat_deg = 0.;
    hud.meters_per_px = 0.;
    hud.zoom_factor = shell.globe_view().camera().distance;
    hud.hud_is_globe = true;
    hud.globe_camera_distance = static_cast<double>(shell.globe_view().camera().distance);
    hud.has_cursor_lonlat = false;
    {
      /// 与 `GlobeEarthView::visible_ground_ew_meters` 一致：水平视场在球面处的近似东西向宽度。
      hud.globe_ground_width_m = shell.globe_view().visible_ground_ew_meters(vp_w, vp_h);
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

  shell.globe_view().setup_projection_and_modelview(vp_w, vp_h);

  {
    double clon = 0.;
    double clat = 0.;
    if (shell.globe_view().try_pixel_lonlat(cursor_mx, cursor_my, vp_w, vp_h, clon, clat)) {
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
    if (shell.globe_view().try_pixel_lonlat(mxc, myc, vp_w, vp_h, globe_viewport_center_lon,
                                             globe_viewport_center_lat)) {
      hud.center_lon_deg = globe_viewport_center_lon;
      hud.center_lat_deg = globe_viewport_center_lat;
      globe_viewport_center_valid = true;
    } else {
      /// `gluUnProject` 偶发失败时仍用姿态解析中心，否则 `use_local_band` 为假且步长按 180° 球冠取，近距下网格极粗或似「消失」。
      shell.globe_view().viewport_center_lonlat_from_pose(globe_viewport_center_lon, globe_viewport_center_lat);
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
        (world_tex_gl != 0U && opts.show_land_basemap) ? world_tex_gl : 0U;
    if (world_vec != nullptr && world_vec->valid()) {
      cw::render::draw_globe_sphere_glsl(0U, kOceanRgb, shell.globe_view().lighting_enabled(), opts.show_land_basemap);
      if (opts.show_land_basemap && !cw::render::globe_merc_atlas_valid()) {
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
      cw::render::draw_globe_sphere_glsl(tex, kOceanRgb, shell.globe_view().lighting_enabled(), opts.show_land_basemap);
    }
  } else if (world_vec != nullptr && world_vec->valid()) {
    draw_globe_sphere(0U);
    if (opts.show_land_basemap) {
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
        (world_tex_gl != 0U && opts.show_land_basemap) ? world_tex_gl : 0U;
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
    cw::render::GlobeEarthView::compute_eye(shell.globe_view().camera().yaw, shell.globe_view().camera().pitch,
                                            shell.globe_view().camera().distance, gex, gey, gez);
    /// 与分屏左侧一致：用战术墨卡托视锥的经纬跨度选步长。纯三维若仅用 `visible_sphere_diameter_deg(d)`，近距时仍接近 180°，与屏上实际小块不一致，网格会过粗或难以辨认。
    const double span_deg = cw::render::tactical_frustum_lonlat_span_deg(tactical, cx_ref);
    const float equiv_d = cw::render::tactical_equiv_camera_distance_from_span_deg(span_deg);
    double grid_step_match = cw::render::pick_lonlat_step_deg(span_deg, equiv_d);
    if (shell.view_mode() == ViewMode::Split2dGlobe) {
      const double split_step = shell.split_matched_lonlat_grid_step_deg();
      if (split_step > 0.0) {
        grid_step_match = split_step;
      }
    }
    cw::render::draw_globe_lonlat_grid(vp_w, vp_h, shell.globe_view().camera().distance, shell.globe_view().content_R(),
                                       static_cast<double>(gex), static_cast<double>(gey), static_cast<double>(gez),
                                       kGlobeGridR, &globe_grid_labels, globe_viewport_center_valid,
                                       globe_viewport_center_lon, globe_viewport_center_lat, grid_step_match);
  }
  glLineWidth(1.F);
  glColor3f(1.F, 1.F, 1.F);

  if (draw_simulation_layers) {
    draw_airspaces_globe(world, cx_ref);
    draw_routes_globe(world, cx_ref, vp_w, vp_h, &globe_grid_labels, shell.globe_view().camera().distance);
    std::unordered_map<cw::engine::EntityId, cw::math::Vec3> pos_by_id;
    for (const auto& e : world.situation.entities) {
      pos_by_id[e.id] = e.position;
    }
    draw_detections_globe(world, pos_by_id, cx_ref);
    draw_entities_globe(world, cx_ref, shell.globe_view(), vp_w, vp_h);
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
  constexpr double kPi = 3.14159265358979323846;
  const double lat_r = hud.center_lat_deg * (kPi / 180.0);
  const double cos_lat = std::max(1e-4, std::cos(lat_r));
  const double m_ew_per_px = hud.meters_per_px * cos_lat;
  const double view_ew_km = m_ew_per_px * static_cast<double>(std::max(1, vp_w)) / 1000.0;
  std::snprintf(line2, sizeof(line2),
                "Scale  1 px = %.2f m (EW)   View EW = %.2f km   Zoom = %.2fx", m_ew_per_px,
                view_ew_km, static_cast<double>(hud.zoom_factor));

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

namespace {

const char* engine_state_hud_label(cw::engine::EngineState s) {
  using cw::engine::EngineState;
  switch (s) {
    case EngineState::Uninitialized:
      return "Uninit";
    case EngineState::Ready:
      return "Ready";
    case EngineState::Running:
      return "Running";
    case EngineState::Paused:
      return "Paused";
    case EngineState::Stopped:
      return "Stopped";
    default:
      return "?";
  }
}

}  // namespace

static double great_circle_deg(double lon1_deg, double lat1_deg, double lon2_deg, double lat2_deg) noexcept {
  constexpr double kPi = 3.14159265358979323846;
  const double p1 = lat1_deg * (kPi / 180.0);
  const double p2 = lat2_deg * (kPi / 180.0);
  const double dlat = (lat2_deg - lat1_deg) * (kPi / 180.0);
  const double dlon = (lon2_deg - lon1_deg) * (kPi / 180.0);
  const double a = std::sin(dlat * 0.5) * std::sin(dlat * 0.5) +
                   std::cos(p1) * std::cos(p2) * std::sin(dlon * 0.5) * std::sin(dlon * 0.5);
  const double c = 2.0 * std::atan2(std::sqrt(std::max(0.0, a)), std::sqrt(std::max(0.0, 1.0 - a)));
  return c * (180.0 / kPi);
}

static std::optional<cw::engine::EntityId> pick_entity_tactical(const cw::engine::SituationPresentation& world,
                                                                cw::render::TacticalMercatorMap& tactical,
                                                                int vp_w, int vp_h, int mx_local,
                                                                int my_local) {
  if (vp_w < 4 || vp_h < 4) {
    return std::nullopt;
  }
  cw::render::MercatorBounds b{};
  tactical.expand_bounds_from_presentation(world, b);
  cw::render::MercatorOrthoFrustum tactical_fr{};
  tactical.compute_interactive_frustum(b, vp_w, vp_h, tactical_fr);
  const float cx_ref = (tactical_fr.l + tactical_fr.r) * 0.5F;
  const float world_width_m = tactical_fr.r - tactical_fr.l;
  const float world_height_m = tactical_fr.t - tactical_fr.b;
  const float vpwf = static_cast<float>(std::max(1, vp_w));
  const float vphf = static_cast<float>(std::max(1, vp_h));
  const float m_per_px_x = world_width_m / vpwf;
  const float m_per_px_y = world_height_m / vphf;
  constexpr float kIconScreenPx = 44.F;
  const float hit_r = 0.5F * kIconScreenPx * std::max(m_per_px_x, m_per_px_y);
  const float hit_r2 = hit_r * hit_r;

  const int px = std::clamp(mx_local, 0, std::max(0, vp_w - 1));
  const int py = std::clamp(my_local, 0, std::max(0, vp_h - 1));
  const int w = std::max(1, vp_w);
  const int h = std::max(1, vp_h);
  const double fx = (static_cast<double>(px) + 0.5) / static_cast<double>(w);
  const double fy = (static_cast<double>(h - 1 - py) + 0.5) / static_cast<double>(h);
  const double wx =
      static_cast<double>(tactical_fr.l) + fx * (static_cast<double>(tactical_fr.r) - static_cast<double>(tactical_fr.l));
  const double wy =
      static_cast<double>(tactical_fr.b) + fy * (static_cast<double>(tactical_fr.t) - static_cast<double>(tactical_fr.b));

  const auto& snap = world.situation;
  const double W = static_cast<double>(cw::render::TacticalMercatorMap::kWorldWidthM);
  std::optional<cw::engine::EntityId> best_id;
  float best_d2 = hit_r2 + 1.0e12F;

  for (const auto& e : snap.entities) {
    const double ex =
        static_cast<double>(cw::render::TacticalMercatorMap::mercator_periodic_x(e.position.x, cx_ref));
    const double ey = static_cast<double>(e.position.y);
    double dx = ex - wx;
    dx -= std::round(dx / W) * W;
    const double dy = ey - wy;
    const double d2 = dx * dx + dy * dy;
    if (d2 <= hit_r2 && static_cast<float>(d2) < best_d2) {
      best_d2 = static_cast<float>(d2);
      best_id = e.id;
    }
  }
  return best_id;
}

static std::optional<cw::engine::EntityId> pick_entity_globe_gl(const cw::engine::SituationPresentation& world,
                                                                cw::render::GlobeEarthView& globe,
                                                                cw::render::TacticalMercatorMap& tactical,
                                                                int tactical_frustum_vp_w, int vp_w, int vp_h,
                                                                int mx_local, int my_local) {
  if (vp_w < 4 || vp_h < 4) {
    return std::nullopt;
  }
  globe.setup_projection_and_modelview(vp_w, vp_h);
  double clon = 0.;
  double clat = 0.;
  if (!globe.try_pixel_lonlat(mx_local, my_local, vp_w, vp_h, clon, clat)) {
    return std::nullopt;
  }
  cw::render::MercatorBounds b{};
  tactical.expand_bounds_from_presentation(world, b);
  cw::render::MercatorOrthoFrustum tactical_fr{};
  tactical.compute_interactive_frustum(b, tactical_frustum_vp_w, vp_h, tactical_fr);
  const float cx_ref = (tactical_fr.l + tactical_fr.r) * 0.5F;

  const double ew_m = globe.visible_ground_ew_meters(vp_w, vp_h);
  constexpr double kEarthR = 6378137.0;
  constexpr double kPi = 3.14159265358979323846;
  const double visible_deg = (ew_m / kEarthR) * (180.0 / kPi);
  const double th_deg =
      std::clamp(visible_deg * (0.5 * 44.0 / static_cast<double>(std::max(vp_w, vp_h))), 0.08, 3.5);

  const auto& snap = world.situation;
  std::optional<cw::engine::EntityId> best_id;
  double best_d = 1.0e100;
  for (const auto& e : snap.entities) {
    double elon = 0.;
    double elat = 0.;
    mercator_meters_to_lonlat(
        static_cast<double>(cw::render::TacticalMercatorMap::mercator_periodic_x(e.position.x, cx_ref)),
        static_cast<double>(e.position.y), elon, elat);
    const double d = great_circle_deg(elon, elat, clon, clat);
    if (d <= th_deg && d < best_d) {
      best_d = d;
      best_id = e.id;
    }
  }
  return best_id;
}

std::optional<cw::engine::EntityId> try_pick_entity_at_screen(const cw::engine::SituationPresentation& world,
                                                                SituationViewShell& shell, int client_w,
                                                                int client_h, int mouse_x, int mouse_y) {
  if (world.situation.entities.empty()) {
    return std::nullopt;
  }
  const ViewMode vm = shell.view_mode();
  const int cw = std::max(1, client_w);
  const int ch = std::max(1, client_h);

  if (vm == ViewMode::Tactical2D) {
    const MapWindow tact = shell.tactical_map_window(cw, std::max(1, client_h));
    if (!tact.contains(mouse_x, mouse_y)) {
      return std::nullopt;
    }
    return pick_entity_tactical(world, shell.tactical_map(), tact.w, tact.h, tact.to_local_x(mouse_x),
                                  tact.to_local_y(mouse_y));
  }

  if (vm == ViewMode::Globe3d) {
    const MapWindow glob = shell.globe_map_window(cw, std::max(1, client_h));
    if (!glob.contains(mouse_x, mouse_y)) {
      return std::nullopt;
    }
    auto id = pick_entity_globe_gl(world, shell.globe_view(), shell.tactical_map(), cw, glob.w, glob.h,
                                   glob.to_local_x(mouse_x), glob.to_local_y(mouse_y));
    glViewport(0, 0, cw, ch);
    return id;
  }

  const MapWindow tact = shell.tactical_map_window(cw, std::max(1, client_h));
  const MapWindow glob = shell.globe_map_window(cw, std::max(1, client_h));
  const int split_x = tact.w;
  if (tact.contains(mouse_x, mouse_y)) {
    return pick_entity_tactical(world, shell.tactical_map(), tact.w, tact.h, tact.to_local_x(mouse_x),
                                tact.to_local_y(mouse_y));
  }
  if (glob.contains(mouse_x, mouse_y)) {
    auto id = pick_entity_globe_gl(world, shell.globe_view(), shell.tactical_map(), split_x, glob.w, glob.h,
                                   glob.to_local_x(mouse_x), glob.to_local_y(mouse_y));
    glViewport(0, 0, cw, ch);
    return id;
  }
  return std::nullopt;
}

void draw_perf_overlay_gl(int vp_w, int vp_h, GLuint font_base, double fps, double frame_ms,
                          cw::render::GraphicsApi present_api) {
  if (font_base == 0 || vp_w < 8 || vp_h < 8) {
    return;
  }
  const char* api_name = present_api == cw::render::GraphicsApi::Vulkan ? "Vulkan" : "OpenGL";
  char line_fps[72];
  char line_ms[72];
  char line_api[96];
  std::snprintf(line_fps, sizeof(line_fps), "FPS %.1f", fps);
  std::snprintf(line_ms, sizeof(line_ms), "Frame %.2f ms", frame_ms);
  std::snprintf(line_api, sizeof(line_api), "Present %s", api_name);

  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0.0, static_cast<double>(vp_w), static_cast<double>(vp_h), 0.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  const int line_h = 18;
  const int y0 = line_h + 4;

  auto draw_ascii = [font_base](const char* s, int x, int y) {
    glColor3f(0.02F, 0.02F, 0.04F);
    glRasterPos2i(x + 1, y + 1);
    glListBase(font_base - 32);
    glCallLists(static_cast<GLsizei>(std::strlen(s)), GL_UNSIGNED_BYTE, reinterpret_cast<const GLubyte*>(s));
    glColor3f(0.85F, 0.92F, 0.78F);
    glRasterPos2i(x, y);
    glListBase(font_base - 32);
    glCallLists(static_cast<GLsizei>(std::strlen(s)), GL_UNSIGNED_BYTE, reinterpret_cast<const GLubyte*>(s));
  };

  draw_ascii(line_fps, 6, y0);
  draw_ascii(line_ms, 6, y0 + line_h);
  draw_ascii(line_api, 6, y0 + 2 * line_h);

  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
}

void draw_simulation_overlay_gl(int vp_w, int vp_h, GLuint font_base, const cw::engine::SituationPresentation& world,
                                bool show_entity_list, std::optional<cw::engine::EntityId> detail_entity,
                                int extra_top_pad_px) {
  if (font_base == 0 || vp_w < 8 || vp_h < 8) {
    return;
  }
  const cw::engine::SituationSnapshot& snap = world.situation;
  char status[192];
  std::snprintf(status, sizeof(status), "Sim t=%.2fs  x%.4g  [%s]", snap.sim_time, snap.time_scale,
                engine_state_hud_label(snap.engine_state));

  glDisable(GL_TEXTURE_2D);
  glDisable(GL_BLEND);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0.0, static_cast<double>(vp_w), static_cast<double>(vp_h), 0.0, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  const int line_h = 18;
  /// `wglUseFontBitmaps` 字形自 RasterPos 向上绘制；y 过小则上半截在视口 y=0 处被裁切，看起来像被菜单挡住。
  const int y_top = line_h + 4 + extra_top_pad_px;

  auto draw_ascii = [font_base](const char* s, int x, int y) {
    glColor3f(0.02F, 0.02F, 0.04F);
    glRasterPos2i(x + 1, y + 1);
    glListBase(font_base - 32);
    glCallLists(static_cast<GLsizei>(std::strlen(s)), GL_UNSIGNED_BYTE,
                reinterpret_cast<const GLubyte*>(s));
    glColor3f(0.85F, 0.92F, 0.78F);
    glRasterPos2i(x, y);
    glListBase(font_base - 32);
    glCallLists(static_cast<GLsizei>(std::strlen(s)), GL_UNSIGNED_BYTE,
                reinterpret_cast<const GLubyte*>(s));
  };

  draw_ascii(status, 6, y_top);
  const int ent_y0 = y_top + line_h;

  const cw::engine::EntitySituation* detail = nullptr;
  if (detail_entity.has_value()) {
    for (const auto& e : snap.entities) {
      if (e.id == *detail_entity) {
        detail = &e;
        break;
      }
    }
  }
  if (detail != nullptr) {
    double plon = 0.;
    double plat = 0.;
    mercator_meters_to_lonlat(static_cast<double>(detail->position.x), static_cast<double>(detail->position.y),
                              plon, plat);
    const float vx = detail->velocity.x;
    const float vy = detail->velocity.y;
    const float vz = detail->velocity.z;
    const float vh = std::sqrt(vx * vx + vy * vy);
    const float vm = std::sqrt(vx * vx + vy * vy + vz * vz);
    const float wx = detail->angular_velocity.x;
    const float wy = detail->angular_velocity.y;
    const float wz = detail->angular_velocity.z;
    const float wm = std::sqrt(wx * wx + wy * wy + wz * wz);
    const char* nm = detail->name.empty() ? detail->external_id.c_str() : detail->name.c_str();
    if (nm[0] == '\0') {
      nm = "(entity)";
    }
    int row = 0;
    char buf[256];
    std::snprintf(buf, sizeof(buf), "[Selected] %.40s  id=%llu", nm,
                  static_cast<unsigned long long>(detail->id));
    draw_ascii(buf, 6, ent_y0 + row * line_h);
    ++row;
    std::snprintf(buf, sizeof(buf), "Lon %.5f  Lat %.5f", plon, plat);
    draw_ascii(buf, 6, ent_y0 + row * line_h);
    ++row;
    std::snprintf(buf, sizeof(buf), "Pos m  x=%.1f y=%.1f z=%.1f", static_cast<double>(detail->position.x),
                  static_cast<double>(detail->position.y), static_cast<double>(detail->position.z));
    draw_ascii(buf, 6, ent_y0 + row * line_h);
    ++row;
    std::snprintf(buf, sizeof(buf), "Vel m/s  horiz=%.2f  |v|=%.2f  (vx,vy,vz)=(%.2f,%.2f,%.2f)", static_cast<double>(vh),
                  static_cast<double>(vm), static_cast<double>(vx), static_cast<double>(vy), static_cast<double>(vz));
    draw_ascii(buf, 6, ent_y0 + row * line_h);
    ++row;
    std::snprintf(buf, sizeof(buf), "Att deg  yaw=%.2f  pitch=%.2f  roll=%.2f",
                  static_cast<double>(detail->yaw_deg), static_cast<double>(detail->pitch_deg),
                  static_cast<double>(detail->roll_deg));
    draw_ascii(buf, 6, ent_y0 + row * line_h);
    ++row;
    std::snprintf(buf, sizeof(buf), "Ang vel rad/s  |w|=%.4f  (wx,wy,wz)=(%.4f,%.4f,%.4f)", static_cast<double>(wm),
                  static_cast<double>(wx), static_cast<double>(wy), static_cast<double>(wz));
    draw_ascii(buf, 6, ent_y0 + row * line_h);
    ++row;
  }

  if (show_entity_list && !snap.entities.empty()) {
    constexpr int kMaxLines = 16;
    constexpr int kApproxCharPx = 12;
    const int n = static_cast<int>(std::min(snap.entities.size(), static_cast<std::size_t>(kMaxLines)));
    for (int i = 0; i < n; ++i) {
      const auto& e = snap.entities[static_cast<std::size_t>(i)];
      char line[96];
      const char* nm = e.name.empty() ? e.external_id.c_str() : e.name.c_str();
      if (nm[0] == '\0') {
        nm = "(entity)";
      }
      std::snprintf(line, sizeof(line), "%u  %.48s", static_cast<unsigned>(e.id), nm);
      const int text_w = static_cast<int>(std::strlen(line)) * kApproxCharPx;
      const int x_right = std::max(6, vp_w - text_w - 8);
      draw_ascii(line, x_right, ent_y0 + i * line_h);
    }
    if (snap.entities.size() > static_cast<std::size_t>(kMaxLines)) {
      char more[48];
      std::snprintf(more, sizeof(more), "... +%zu more", snap.entities.size() - kMaxLines);
      const int text_w = static_cast<int>(std::strlen(more)) * kApproxCharPx;
      const int x_right = std::max(6, vp_w - text_w - 8);
      draw_ascii(more, x_right, ent_y0 + n * line_h);
    }
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

void draw_frame_tactical(const cw::engine::SituationPresentation& world, SituationViewShell& shell, int vp_w,
                         int vp_h, int cursor_mx, int cursor_my, const cw::render::WorldVectorMerc* world_vec,
                         unsigned world_tex_gl, const cw::render::WorldVectorLines* coastlines,
                         const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                         bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base,
                         bool clear_color_buffer, const SituationRenderOptions& opts) {
  cw::render::MercatorBounds b{};
  shell.tactical_map().expand_bounds_from_presentation(world, b);
  cw::render::MercatorOrthoFrustum tactical{};
  shell.tactical_map().compute_interactive_frustum(b, vp_w, vp_h, tactical);
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
    hud.zoom_factor = shell.tactical_map().zoom();
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
    draw_world_underlay_vector(basemap, world_vec, opts.show_land_basemap);
  } else if (world_tex_gl != 0 && opts.show_land_basemap) {
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
    draw_airspaces(world, cx_ref);
    {
      const float ww = tactical.r - tactical.l;
      glLineWidth(std::clamp(ww / 200000.F, 1.F, 6.F));
    }
    {
      const float ww = tactical.r - tactical.l;
      const float hh = tactical.t - tactical.b;
      draw_routes(world, cx_ref, ww, hh, vp_w, vp_h, hud_font_base);
    }

    std::unordered_map<cw::engine::EntityId, cw::math::Vec3> pos_by_id;
    for (const auto& e : world.situation.entities) {
      pos_by_id[e.id] = e.position;
    }
    draw_detections(world, pos_by_id, cx_ref);
    draw_entities(world, tactical.r - tactical.l, tactical.t - tactical.b, vp_w, vp_h, cx_ref,
                  icon_cache);
  }

  glLineWidth(1.F);

#ifdef _WIN32
  if (hud_font_base != 0) {
    draw_hud_gl(vp_w, vp_h, hud_font_base, hud, &tactical_grid_labels);
  }
#endif
}

void draw_frame_split(const cw::engine::SituationPresentation& world, SituationViewShell& shell, int vp_w,
                      int vp_h, int cursor_mx, int cursor_my, const cw::render::WorldVectorMerc* world_vec,
                      unsigned world_tex_gl, const cw::render::WorldVectorLines* coastlines,
                      const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                      bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base, const SituationRenderOptions& opts) {
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
  draw_frame_tactical(world, shell, split_x, vp_h, t_mx, t_my, world_vec, world_tex_gl, coastlines, boundary_lines,
                      icon_cache, draw_simulation_layers, hud_out, hud_font_base, false, opts);

  glViewport(split_x, 0, right_w, vp_h);
  draw_frame_globe(world, shell, right_w, vp_h, split_x, g_mx, g_my, world_vec, world_tex_gl, coastlines,
                   boundary_lines, icon_cache, draw_simulation_layers, hud_out, hud_font_base, false, opts);

  glViewport(0, 0, vp_w, vp_h);
  draw_split_divider(vp_w, vp_h, split_x);
}

void draw_frame(const cw::engine::SituationPresentation& world, SituationViewShell& shell, int vp_w, int vp_h,
                int cursor_mx, int cursor_my, const cw::render::WorldVectorMerc* world_vec, unsigned world_tex_gl,
                const cw::render::WorldVectorLines* coastlines,
                const cw::render::WorldVectorLines* boundary_lines, IconTextureCache& icon_cache,
                bool draw_simulation_layers, SituationHud* hud_out, GLuint hud_font_base, const SituationRenderOptions& opts,
                double perf_fps, double perf_frame_ms, cw::render::GraphicsApi perf_present_api) {
  if (shell.view_mode() == ViewMode::Tactical2D) {
    shell.reset_globe_auxiliary_state();
  }
  if (shell.view_mode() == ViewMode::Globe3d) {
    draw_frame_globe(world, shell, vp_w, vp_h, vp_w, cursor_mx, cursor_my, world_vec, world_tex_gl, coastlines,
                      boundary_lines, icon_cache, draw_simulation_layers, hud_out, hud_font_base, true, opts);
  } else if (shell.view_mode() == ViewMode::Split2dGlobe) {
    draw_frame_split(world, shell, vp_w, vp_h, cursor_mx, cursor_my, world_vec, world_tex_gl, coastlines,
                      boundary_lines, icon_cache, draw_simulation_layers, hud_out, hud_font_base, opts);
  } else {
    draw_frame_tactical(world, shell, vp_w, vp_h, cursor_mx, cursor_my, world_vec, world_tex_gl, coastlines,
                        boundary_lines, icon_cache, draw_simulation_layers, hud_out, hud_font_base, true, opts);
  }
#ifdef _WIN32
  glViewport(0, 0, vp_w, vp_h);
  constexpr int kPerfHudLines = 3;
  constexpr int kHudLinePx = 18;
  const int sim_extra_pad = (hud_font_base != 0) ? (kPerfHudLines * kHudLinePx) : 0;
  if (hud_font_base != 0) {
    draw_perf_overlay_gl(vp_w, vp_h, hud_font_base, perf_fps, perf_frame_ms, perf_present_api);
  }
  if (hud_font_base != 0 && draw_simulation_layers) {
    draw_simulation_overlay_gl(vp_w, vp_h, hud_font_base, world, true, shell.picked_entity_id(), sim_extra_pad);
  }
#else
  (void)perf_fps;
  (void)perf_frame_ms;
  (void)perf_present_api;
#endif
}

}  // namespace cw::situation_view
