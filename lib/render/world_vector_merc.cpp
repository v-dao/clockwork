#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "cw/render/world_vector_merc.hpp"

#include "cw/render/mercator_geo.hpp"
#include "cw/log.hpp"

#include <GL/glu.h>

#include <array>
#include <cstdint>
#include <cmath>
#include <deque>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace cw::render {
namespace {

void CALLBACK tess_begin_cb(GLenum type) { glBegin(type); }

void CALLBACK tess_vertex_cb(void* data) { glVertex3dv(static_cast<GLdouble*>(data)); }

double g_land_sphere_radius = 1.0;

void CALLBACK tess_vertex_sphere_cb(void* data) {
  GLdouble* p = static_cast<GLdouble*>(data);
  double lon = 0.;
  double lat = 0.;
  mercator_meters_to_lonlat(p[0], p[1], lon, lat);
  double x = 0.;
  double y = 0.;
  double z = 0.;
  lonlat_deg_to_unit_sphere(lon, lat, x, y, z);
  p[0] = x * g_land_sphere_radius;
  p[1] = y * g_land_sphere_radius;
  p[2] = z * g_land_sphere_radius;
  glVertex3dv(p);
}

void CALLBACK tess_end_cb() { glEnd(); }

void CALLBACK tess_error_cb(GLenum err) {
  const GLubyte* msg = gluErrorString(err);
  cw::log(cw::LogLevel::Error,
          std::string("world_vector_merc: GLU tessellation error: ").append(
              reinterpret_cast<const char*>(msg)));
}

std::deque<std::array<GLdouble, 3>> g_combine_ring;

void CALLBACK tess_combine_cb(GLdouble coords[3], void* vertex_data[4], GLfloat weight[4],
                              void** out_data) {
  (void)vertex_data;
  (void)weight;
  g_combine_ring.push_back({coords[0], coords[1], coords[2]});
  *out_data = g_combine_ring.back().data();
}

struct MercPolygon {
  std::vector<std::vector<std::pair<float, float>>> rings;
};

bool read_u32(std::ifstream& in, std::uint32_t& out) {
  in.read(reinterpret_cast<char*>(&out), 4);
  return in.gcount() == 4;
}

[[nodiscard]] bool parse_merc2(std::ifstream& in, std::vector<MercPolygon>& out_polys) {
  char magic[4]{};
  in.read(magic, 4);
  if (!in || magic[0] != 'C' || magic[1] != 'W' || magic[2] != 'v' || magic[3] != '2') {
    return false;
  }

  std::uint32_t ver = 0;
  if (!read_u32(in, ver) || ver != 2U) {
    return false;
  }

  std::uint32_t num_polygons = 0;
  if (!read_u32(in, num_polygons) || num_polygons == 0U || num_polygons > 500000U) {
    return false;
  }

  out_polys.resize(num_polygons);

  for (std::uint32_t pi = 0; pi < num_polygons; ++pi) {
    std::uint32_t num_rings = 0;
    if (!read_u32(in, num_rings) || num_rings == 0U || num_rings > 100000U) {
      return false;
    }

    out_polys[pi].rings.resize(num_rings);

    for (std::uint32_t ri = 0; ri < num_rings; ++ri) {
      std::uint32_t n = 0;
      if (!read_u32(in, n) || n < 3U || n > 10000000U) {
        return false;
      }

      auto& ring = out_polys[pi].rings[ri];
      ring.resize(n);

      for (std::uint32_t k = 0; k < n; ++k) {
        float x = 0.F;
        float y = 0.F;
        in.read(reinterpret_cast<char*>(&x), 4);
        in.read(reinterpret_cast<char*>(&y), 4);
        if (!in) {
          return false;
        }
        ring[k] = {x, y};
      }
    }
  }

  return !in.fail();
}

}  // namespace

/// 将球面填充用的岸线在墨卡托平面上细分，避免三角面片过大时在 3D 中「切穿」球体形成条纹。
static void subdivide_ring_for_globe(std::vector<std::pair<float, float>>& ring, float max_edge_m) {
  if (ring.size() < 3U) {
    return;
  }
  const float max_e = std::max(5000.F, max_edge_m);
  /// 去掉与首点重合的闭合重复点，便于按环分段。
  while (ring.size() > 3U) {
    const auto& a = ring.front();
    const auto& b = ring.back();
    const float ddx = a.first - b.first;
    const float ddy = a.second - b.second;
    if (ddx * ddx + ddy * ddy < 4.F) {
      ring.pop_back();
    } else {
      break;
    }
  }
  const std::size_t n = ring.size();
  if (n < 3U) {
    return;
  }
  std::vector<std::pair<float, float>> out;
  out.reserve(n * 4U);
  for (std::size_t i = 0; i < n; ++i) {
    const auto& a = ring[i];
    const auto& b = ring[(i + 1U) % n];
    const float dx = b.first - a.first;
    const float dy = b.second - a.second;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4F) {
      continue;
    }
    int nseg = std::max(1, static_cast<int>(std::ceil(static_cast<double>(len / max_e))));
    nseg = std::min(nseg, 8192);
    for (int k = 0; k < nseg; ++k) {
      const float t = static_cast<float>(k) / static_cast<float>(nseg);
      out.push_back({a.first + t * dx, a.second + t * dy});
    }
  }
  if (out.size() >= 3U) {
    ring = std::move(out);
  }
}

static void subdivide_polygons_for_globe(std::vector<WorldVectorMercPolygon>& polys,
                                        float max_edge_m) {
  for (auto& poly : polys) {
    for (auto& ring : poly.rings) {
      subdivide_ring_for_globe(ring, max_edge_m);
    }
  }
}

bool WorldVectorMerc::load_from_file(const char* path_utf8) noexcept {
  destroy();

  std::ifstream in(path_utf8, std::ios::binary);
  if (!in) {
    return false;
  }

  std::vector<MercPolygon> polys;
  if (!parse_merc2(in, polys)) {
    return false;
  }

  GLUtesselator* tess = gluNewTess();
  if (tess == nullptr) {
    cw::log(cw::LogLevel::Error, "world_vector_merc: gluNewTess failed");
    return false;
  }

  gluTessCallback(tess, GLU_TESS_BEGIN, reinterpret_cast<void (APIENTRY*)()>(tess_begin_cb));
  gluTessCallback(tess, GLU_TESS_VERTEX, reinterpret_cast<void (APIENTRY*)()>(tess_vertex_cb));
  gluTessCallback(tess, GLU_TESS_END, reinterpret_cast<void (APIENTRY*)()>(tess_end_cb));
  gluTessCallback(tess, GLU_TESS_ERROR, reinterpret_cast<void (APIENTRY*)()>(tess_error_cb));
  gluTessCallback(tess, GLU_TESS_COMBINE, reinterpret_cast<void (APIENTRY*)()>(tess_combine_cb));
  gluTessProperty(tess, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
  static const GLdouble kNormal[3] = {0.0, 0.0, 1.0};
  gluTessNormal(tess, kNormal[0], kNormal[1], kNormal[2]);

  land_display_list = glGenLists(1);
  if (land_display_list == 0) {
    gluDeleteTess(tess);
    return false;
  }

  glNewList(land_display_list, GL_COMPILE);

  std::vector<GLdouble> coord_buf;

  for (const auto& poly : polys) {
    g_combine_ring.clear();

    std::size_t n_pts = 0;
    for (const auto& ring : poly.rings) {
      n_pts += ring.size();
    }
    coord_buf.clear();
    coord_buf.reserve(n_pts * 3);

    gluTessBeginPolygon(tess, nullptr);

    for (const auto& ring : poly.rings) {
      gluTessBeginContour(tess);
      for (const auto& pt : ring) {
        const std::size_t o = coord_buf.size();
        coord_buf.push_back(static_cast<GLdouble>(pt.first));
        coord_buf.push_back(static_cast<GLdouble>(pt.second));
        coord_buf.push_back(0.0);
        gluTessVertex(tess, &coord_buf[o], &coord_buf[o]);
      }
      gluTessEndContour(tess);
    }

    gluTessEndPolygon(tess);
  }

  glEndList();
  gluDeleteTess(tess);

  polygons.clear();
  polygons.reserve(polys.size());
  for (auto& p : polys) {
    WorldVectorMercPolygon wp;
    wp.rings = std::move(p.rings);
    polygons.push_back(std::move(wp));
  }
  subdivide_polygons_for_globe(polygons, 150000.F);

  return true;
}

void WorldVectorMerc::draw_land_fill() const noexcept {
  if (land_display_list != 0) {
    glCallList(land_display_list);
  }
}

void WorldVectorMerc::draw_land_fill_sphere(double radius) const noexcept {
  if (land_display_list == 0 || polygons.empty()) {
    return;
  }
  g_land_sphere_radius = radius;

  GLUtesselator* tess = gluNewTess();
  if (tess == nullptr) {
    return;
  }

  gluTessCallback(tess, GLU_TESS_BEGIN, reinterpret_cast<void (APIENTRY*)()>(tess_begin_cb));
  gluTessCallback(tess, GLU_TESS_VERTEX, reinterpret_cast<void (APIENTRY*)()>(tess_vertex_sphere_cb));
  gluTessCallback(tess, GLU_TESS_END, reinterpret_cast<void (APIENTRY*)()>(tess_end_cb));
  gluTessCallback(tess, GLU_TESS_ERROR, reinterpret_cast<void (APIENTRY*)()>(tess_error_cb));
  gluTessCallback(tess, GLU_TESS_COMBINE, reinterpret_cast<void (APIENTRY*)()>(tess_combine_cb));
  gluTessProperty(tess, GLU_TESS_WINDING_RULE, GLU_TESS_WINDING_ODD);
  static const GLdouble kNormal[3] = {0.0, 0.0, 1.0};
  gluTessNormal(tess, kNormal[0], kNormal[1], kNormal[2]);

  std::vector<GLdouble> coord_buf;

  for (const auto& poly : polygons) {
    g_combine_ring.clear();

    std::size_t n_pts = 0;
    for (const auto& ring : poly.rings) {
      n_pts += ring.size();
    }
    coord_buf.clear();
    coord_buf.reserve(n_pts * 3);

    gluTessBeginPolygon(tess, nullptr);

    for (const auto& ring : poly.rings) {
      gluTessBeginContour(tess);
      for (const auto& pt : ring) {
        const std::size_t o = coord_buf.size();
        coord_buf.push_back(static_cast<GLdouble>(pt.first));
        coord_buf.push_back(static_cast<GLdouble>(pt.second));
        coord_buf.push_back(0.0);
        gluTessVertex(tess, &coord_buf[o], &coord_buf[o]);
      }
      gluTessEndContour(tess);
    }

    gluTessEndPolygon(tess);
  }

  gluDeleteTess(tess);
}

void WorldVectorMerc::destroy() noexcept {
  if (land_display_list != 0) {
    glDeleteLists(land_display_list, 1);
    land_display_list = 0;
  }
  polygons.clear();
}

}  // namespace cw::render
