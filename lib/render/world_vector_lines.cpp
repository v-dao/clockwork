#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "cw/render/world_vector_lines.hpp"

#include "cw/render/mercator_geo.hpp"
#include "cw/log.hpp"

#include <cstdint>
#include <fstream>
#include <string>

namespace cw::render {
namespace {

bool read_u32(std::ifstream& in, std::uint32_t& out) {
  in.read(reinterpret_cast<char*>(&out), 4);
  return in.gcount() == 4;
}

}  // namespace

bool WorldVectorLines::load_from_file(const char* path_utf8) noexcept {
  destroy();

  std::ifstream in(path_utf8, std::ios::binary);
  if (!in) {
    return false;
  }

  char magic[4]{};
  in.read(magic, 4);
  if (!in || magic[0] != 'C' || magic[1] != 'W' || magic[2] != 'l' || magic[3] != '1') {
    return false;
  }

  std::uint32_t ver = 0;
  if (!read_u32(in, ver) || ver != 1U) {
    return false;
  }

  std::uint32_t num_strips = 0;
  if (!read_u32(in, num_strips) || num_strips == 0U || num_strips > 2'000'000U) {
    return false;
  }

  line_display_list = glGenLists(1);
  if (line_display_list == 0) {
    return false;
  }

  glNewList(line_display_list, GL_COMPILE);

  strips.clear();
  strips.reserve(static_cast<std::size_t>(num_strips));

  for (std::uint32_t si = 0; si < num_strips; ++si) {
    std::uint32_t n = 0;
    if (!read_u32(in, n) || n < 2U || n > 5'000'000U) {
      glEndList();
      glDeleteLists(line_display_list, 1);
      line_display_list = 0;
      strips.clear();
      return false;
    }

    std::vector<std::pair<float, float>> strip;
    strip.reserve(static_cast<std::size_t>(n));

    glBegin(GL_LINE_STRIP);
    for (std::uint32_t k = 0; k < n; ++k) {
      float x = 0.F;
      float y = 0.F;
      in.read(reinterpret_cast<char*>(&x), 4);
      in.read(reinterpret_cast<char*>(&y), 4);
      if (!in) {
        glEnd();
        glEndList();
        glDeleteLists(line_display_list, 1);
        line_display_list = 0;
        strips.clear();
        return false;
      }
      strip.emplace_back(x, y);
      glVertex2f(x, y);
    }
    glEnd();
    strips.push_back(std::move(strip));
  }

  glEndList();

  if (!in) {
    glDeleteLists(line_display_list, 1);
    line_display_list = 0;
    strips.clear();
    return false;
  }

  return true;
}

void WorldVectorLines::draw() const noexcept {
  if (line_display_list != 0) {
    glCallList(line_display_list);
  }
}

void WorldVectorLines::draw_on_unit_sphere(double radius) const noexcept {
  if (strips.empty()) {
    return;
  }
  for (const auto& strip : strips) {
    if (strip.size() < 2U) {
      continue;
    }
    glBegin(GL_LINE_STRIP);
    for (const auto& p : strip) {
      double lon = 0.;
      double lat = 0.;
      mercator_meters_to_lonlat(static_cast<double>(p.first), static_cast<double>(p.second), lon, lat);
      double x = 0.;
      double y = 0.;
      double z = 0.;
      lonlat_deg_to_unit_sphere(lon, lat, x, y, z);
      glVertex3d(x * radius, y * radius, z * radius);
    }
    glEnd();
  }
}

void WorldVectorLines::destroy() noexcept {
  if (line_display_list != 0) {
    glDeleteLists(line_display_list, 1);
    line_display_list = 0;
  }
  strips.clear();
}

}  // namespace cw::render
