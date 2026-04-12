#include "cw/render/svg_line_texture.hpp"
#include "cw/render/texture_bmp.hpp"
#include "cw/log.hpp"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace cw::render {

namespace {

void log_svg_fail_once(const std::string& msg) noexcept {
  static std::unordered_set<std::string> seen;
  if (seen.insert(msg).second) {
    cw::log(cw::LogLevel::Warn, msg);
  }
}

FILE* fopen_utf8_rb(const char* path_utf8) noexcept {
#ifdef _WIN32
  const int nw = MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, nullptr, 0);
  if (nw <= 0) {
    return std::fopen(path_utf8, "rb");
  }
  std::vector<wchar_t> w(static_cast<std::size_t>(nw));
  MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, w.data(), nw);
  return _wfopen(w.data(), L"rb");
#else
  return std::fopen(path_utf8, "rb");
#endif
}

bool read_file_all(const char* path_utf8, std::string& out) noexcept {
  out.clear();
  if (path_utf8 == nullptr || path_utf8[0] == '\0') {
    return false;
  }
  FILE* f = fopen_utf8_rb(path_utf8);
  if (f == nullptr) {
    return false;
  }
  if (std::fseek(f, 0, SEEK_END) != 0) {
    std::fclose(f);
    return false;
  }
  const long sz = std::ftell(f);
  if (sz <= 0) {
    std::fclose(f);
    return false;
  }
  if (std::fseek(f, 0, SEEK_SET) != 0) {
    std::fclose(f);
    return false;
  }
  out.resize(static_cast<std::size_t>(sz));
  const std::size_t nread = std::fread(out.data(), 1, out.size(), f);
  std::fclose(f);
  if (nread != out.size()) {
    out.clear();
    return false;
  }
  return true;
}

void trim_ascii(std::string_view& sv) {
  while (!sv.empty() && static_cast<unsigned char>(sv.front()) <= ' ') {
    sv.remove_prefix(1);
  }
  while (!sv.empty() && static_cast<unsigned char>(sv.back()) <= ' ') {
    sv.remove_suffix(1);
  }
}

bool parse_float_token(std::string_view s, float& out) noexcept {
  trim_ascii(s);
  if (s.empty()) {
    return false;
  }
  std::string tmp(s);
  char* end = nullptr;
  const float v = std::strtof(tmp.c_str(), &end);
  if (end == tmp.c_str() || end == nullptr) {
    return false;
  }
  while (*end != '\0' && std::isspace(static_cast<unsigned char>(*end))) {
    ++end;
  }
  if (*end != '\0') {
    // 允许 "16px"
    if (end[0] == 'p' && end[1] == 'x' && end[2] == '\0') {
      // ok
    } else {
      return false;
    }
  }
  out = v;
  return std::isfinite(v);
}

bool ieq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

bool parse_next_attr(std::string_view tag, std::size_t& pos, std::string_view& name,
                     std::string_view& value) {
  while (pos < tag.size() && static_cast<unsigned char>(tag[pos]) <= ' ') {
    ++pos;
  }
  if (pos >= tag.size() || tag[pos] == '>' || (tag[pos] == '/' && pos + 1 < tag.size() &&
                                                tag[pos + 1] == '>')) {
    return false;
  }
  const std::size_t eq = tag.find('=', pos);
  if (eq == std::string_view::npos) {
    return false;
  }
  name = tag.substr(pos, eq - pos);
  trim_ascii(name);
  std::size_t i = eq + 1;
  while (i < tag.size() && static_cast<unsigned char>(tag[i]) <= ' ') {
    ++i;
  }
  if (i >= tag.size()) {
    return false;
  }
  const char q = tag[i];
  if (q == '"' || q == '\'') {
    const std::size_t j = tag.find(q, i + 1);
    if (j == std::string_view::npos) {
      return false;
    }
    value = tag.substr(i + 1, j - i - 1);
    pos = j + 1;
  } else {
    std::size_t j = i;
    while (j < tag.size() && tag[j] != '>' && !std::isspace(static_cast<unsigned char>(tag[j]))) {
      ++j;
    }
    value = tag.substr(i, j - i);
    pos = j;
  }
  return true;
}

bool find_attr_float(std::string_view tag, const char* key, float& out) {
  std::size_t pos = 0;
  std::string_view n;
  std::string_view v;
  while (parse_next_attr(tag, pos, n, v)) {
    if (ieq(n, key)) {
      return parse_float_token(v, out);
    }
  }
  return false;
}

bool find_attr_string(std::string_view tag, const char* key, std::string_view& out) {
  std::size_t pos = 0;
  std::string_view n;
  std::string_view v;
  while (parse_next_attr(tag, pos, n, v)) {
    if (ieq(n, key)) {
      out = v;
      return true;
    }
  }
  return false;
}

std::size_t find_ci(const std::string& s, const char* needle) {
  const std::size_t nlen = std::strlen(needle);
  if (nlen == 0 || s.size() < nlen) {
    return std::string::npos;
  }
  for (std::size_t i = 0; i + nlen <= s.size(); ++i) {
    bool ok = true;
    for (std::size_t j = 0; j < nlen; ++j) {
      if (std::tolower(static_cast<unsigned char>(s[i + j])) !=
          static_cast<unsigned char>(needle[j])) {
        ok = false;
        break;
      }
    }
    if (ok) {
      return i;
    }
  }
  return std::string::npos;
}

struct SvgExtents {
  float vb_min_x = 0.F;
  float vb_min_y = 0.F;
  float vb_w = 16.F;
  float vb_h = 16.F;
};

bool parse_svg_open_tag(const std::string& content, SvgExtents& ex) {
  const std::size_t at = find_ci(content, "<svg");
  if (at == std::string::npos) {
    return false;
  }
  const std::size_t gt = content.find('>', at);
  if (gt == std::string::npos) {
    return false;
  }
  const std::string_view tag(content.c_str() + at, gt - at + 1);
  float w = 0.F;
  float h = 0.F;
  const bool has_w = find_attr_float(tag, "width", w);
  const bool has_h = find_attr_float(tag, "height", h);
  std::string_view vb_str;
  if (find_attr_string(tag, "viewBox", vb_str)) {
    trim_ascii(vb_str);
    float a = 0.F;
    float b = 0.F;
    float c = 0.F;
    float d = 0.F;
    std::string tmp(vb_str);
    char* p = tmp.data();
    char* end = nullptr;
    a = std::strtof(p, &end);
    if (end == p) {
      return false;
    }
    p = end;
    b = std::strtof(p, &end);
    if (end == p) {
      return false;
    }
    p = end;
    c = std::strtof(p, &end);
    if (end == p) {
      return false;
    }
    p = end;
    d = std::strtof(p, &end);
    if (end == p) {
      return false;
    }
    ex.vb_min_x = a;
    ex.vb_min_y = b;
    ex.vb_w = std::max(c, 1e-6F);
    ex.vb_h = std::max(d, 1e-6F);
  } else {
    ex.vb_min_x = 0.F;
    ex.vb_min_y = 0.F;
    ex.vb_w = has_w ? std::max(w, 1e-6F) : 16.F;
    ex.vb_h = has_h ? std::max(h, 1e-6F) : 16.F;
  }
  return true;
}

void stamp_disc(std::vector<std::uint8_t>& rgba, int w, int h, int cx, int cy, int r) {
  if (r < 1) {
    r = 1;
  }
  const int r2 = r * r;
  for (int dy = -r; dy <= r; ++dy) {
    for (int dx = -r; dx <= r; ++dx) {
      if (dx * dx + dy * dy > r2) {
        continue;
      }
      const int x = cx + dx;
      const int y = cy + dy;
      if (x >= 0 && x < w && y >= 0 && y < h) {
        const std::size_t i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(w) +
                               static_cast<std::size_t>(x)) *
                              4U;
        rgba[i] = 255;
        rgba[i + 1] = 255;
        rgba[i + 2] = 255;
        rgba[i + 3] = 255;
      }
    }
  }
}

void bresenham_stroke_rgba(std::vector<std::uint8_t>& rgba, int w, int h, int x0, int y0, int x1,
                           int y1, float stroke_half_px) {
  int r = static_cast<int>(std::lround(stroke_half_px));
  if (r < 1) {
    r = 1;
  }
  int dx = std::abs(x1 - x0);
  int dy = -std::abs(y1 - y0);
  const int sx = x0 < x1 ? 1 : -1;
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  int x = x0;
  int y = y0;
  for (;;) {
    stamp_disc(rgba, w, h, x, y, r);
    if (x == x1 && y == y1) {
      break;
    }
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y += sy;
    }
  }
}

/// 去掉 `<line` 前缀，parse_next_attr 只认 `name="value"`，不能把 `<` 留在首段否则首属性名会解析成 "<line x2"。
std::string_view line_element_attr_text(std::string_view full_open_tag) noexcept {
  if (full_open_tag.size() < 5U) {
    return full_open_tag;
  }
  if (full_open_tag[0] != '<') {
    return full_open_tag;
  }
  constexpr char kLine[] = {'<', 'l', 'i', 'n', 'e'};
  for (std::size_t k = 0; k < 5U; ++k) {
    if (std::tolower(static_cast<unsigned char>(full_open_tag[k])) !=
        static_cast<unsigned char>(kLine[k])) {
      return full_open_tag;
    }
  }
  std::size_t i = 5U;
  while (i < full_open_tag.size() && static_cast<unsigned char>(full_open_tag[i]) <= ' ') {
    ++i;
  }
  return full_open_tag.substr(i);
}

bool parse_line_tag_coords(std::string_view tag, float& x1, float& y1, float& x2, float& y2) {
  const std::string_view attrs = line_element_attr_text(tag);
  bool ok = true;
  ok = ok && find_attr_float(attrs, "x1", x1);
  ok = ok && find_attr_float(attrs, "y1", y1);
  ok = ok && find_attr_float(attrs, "x2", x2);
  ok = ok && find_attr_float(attrs, "y2", y2);
  return ok;
}

bool parse_line_tag_stroke_width(std::string_view tag, float& stroke_width_user) {
  stroke_width_user = 1.F;
  const std::string_view attrs = line_element_attr_text(tag);
  if (find_attr_float(attrs, "stroke-width", stroke_width_user)) {
    if (stroke_width_user < 0.05F || !std::isfinite(stroke_width_user)) {
      stroke_width_user = 1.F;
    }
    return true;
  }
  return false;
}

}  // namespace

bool load_svg_line_icon_texture(const char* path_utf8, Texture2DRgb& out, int raster_px) noexcept {
  out = Texture2DRgb{};
  if (path_utf8 == nullptr || path_utf8[0] == '\0' || raster_px < 8 || raster_px > 4096) {
    return false;
  }

  std::string content;
  if (!read_file_all(path_utf8, content)) {
    log_svg_fail_once(std::string("svg_line_texture: cannot read file: ").append(path_utf8));
    return false;
  }

  SvgExtents ex{};
  if (!parse_svg_open_tag(content, ex)) {
    log_svg_fail_once(std::string("svg_line_texture: missing or invalid root <svg> in: ").append(
        path_utf8));
    return false;
  }

  const int w = raster_px;
  const int h = raster_px;
  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w * h * 4), 0);

  std::size_t search = 0;
  int line_count = 0;
  for (;;) {
    const std::size_t li = find_ci(content.substr(search), "<line");
    if (li == std::string::npos) {
      break;
    }
    const std::size_t abs = search + li;
    // 避免将 <linearGradient 的前缀 <line 误认为 <line 元素。
    if (abs + 5U < content.size()) {
      const unsigned char c = static_cast<unsigned char>(content[abs + 5U]);
      if (c != ' ' && c != '\t' && c != '\r' && c != '\n' && c != '/' && c != '>') {
        search = abs + 1U;
        continue;
      }
    }
    const std::size_t gt = content.find('>', abs);
    if (gt == std::string::npos) {
      break;
    }
    const std::string_view tag(content.c_str() + abs, gt - abs + 1);
    search = gt + 1;

    float x1 = 0.F;
    float y1 = 0.F;
    float x2 = 0.F;
    float y2 = 0.F;
    if (!parse_line_tag_coords(tag, x1, y1, x2, y2)) {
      continue;
    }
    float stroke_width_user = 1.F;
    (void)parse_line_tag_stroke_width(tag, stroke_width_user);
    ++line_count;

    const float sx = static_cast<float>(w - 1) / ex.vb_w;
    const float sy = static_cast<float>(h - 1) / ex.vb_h;
    const int xa = static_cast<int>(std::lround((x1 - ex.vb_min_x) * sx));
    const int ya = static_cast<int>(std::lround((y1 - ex.vb_min_y) * sy));
    const int xb = static_cast<int>(std::lround((x2 - ex.vb_min_x) * sx));
    const int yb = static_cast<int>(std::lround((y2 - ex.vb_min_y) * sy));
    // stroke-width（用户空间）→ 像素；设下限使观感接近粗描边参考图
    float stroke_px = stroke_width_user * 0.5F * (sx + sy);
    stroke_px *= 1.12F;
    const float min_dim = static_cast<float>((w < h) ? w : h);
    stroke_px = std::max(stroke_px, min_dim * 0.095F);
    const float stroke_half_px = 0.5F * stroke_px;
    bresenham_stroke_rgba(rgba, w, h, xa, ya, xb, yb, stroke_half_px);
  }
  if (line_count == 0) {
    log_svg_fail_once(
        std::string("svg_line_texture: no drawable <line> elements (need x1,y1,x2,y2) in: ")
            .append(path_utf8));
    return false;
  }

  if (!upload_texture_rgba(rgba.data(), w, h, out)) {
    log_svg_fail_once(
        std::string("svg_line_texture: OpenGL upload failed (see upload_texture_rgba log): ")
            .append(path_utf8));
    return false;
  }
  return true;
}

}  // namespace cw::render
