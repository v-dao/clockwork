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

#include <GL/gl.h>

#include <cstdint>
#include <cstdio>
#include <vector>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

namespace cw::render {

bool upload_texture_rgba(const std::uint8_t* rgba, int w, int h, Texture2DRgb& out) noexcept {
  out = Texture2DRgb{};
  if (rgba == nullptr || w < 1 || h < 1) {
    return false;
  }
  GLuint name = 0;
  glGenTextures(1, &name);
  if (name == 0) {
    static bool logged = false;
    if (!logged) {
      logged = true;
      cw::log(cw::LogLevel::Warn,
              "upload_texture_rgba: glGenTextures returned 0 (no current GL context or no texture "
              "objects)");
    }
    return false;
  }
  glBindTexture(GL_TEXTURE_2D, name);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
  glBindTexture(GL_TEXTURE_2D, 0);
  out.gl_name = static_cast<unsigned>(name);
  out.width = w;
  out.height = h;
  return true;
}

namespace {

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

#pragma pack(push, 1)
struct BmpFileHeader {
  std::uint16_t type = 0;
  std::uint32_t file_size = 0;
  std::uint16_t reserved1 = 0;
  std::uint16_t reserved2 = 0;
  std::uint32_t off_bits = 0;
};

struct BmpInfoHeader {
  std::uint32_t size = 0;
  std::int32_t width = 0;
  std::int32_t height = 0;
  std::uint16_t planes = 0;
  std::uint16_t bit_count = 0;
  std::uint32_t compression = 0;
  std::uint32_t size_image = 0;
  std::int32_t x_pels_per_meter = 0;
  std::int32_t y_pels_per_meter = 0;
  std::uint32_t clr_used = 0;
  std::uint32_t clr_important = 0;
};
#pragma pack(pop)

constexpr std::uint32_t kBmpRgb = 0L;

bool read_all(FILE* f, void* dst, std::size_t n) {
  return std::fread(dst, 1, n, f) == n;
}

bool load_texture_bmp_rgb24_impl(const char* path_utf8, Texture2DRgb& out, bool white_key) noexcept {
  out = Texture2DRgb{};
  if (path_utf8 == nullptr || path_utf8[0] == '\0') {
    return false;
  }

  FILE* f = fopen_utf8_rb(path_utf8);
  if (f == nullptr) {
    return false;
  }

  BmpFileHeader fh{};
  BmpInfoHeader ih{};
  if (!read_all(f, &fh, sizeof(fh)) || !read_all(f, &ih, sizeof(ih))) {
    std::fclose(f);
    return false;
  }

  if (fh.type != 0x4D42) {
    std::fclose(f);
    return false;
  }
  if (ih.bit_count != 24 || ih.compression != kBmpRgb) {
    std::fclose(f);
    return false;
  }
  if (ih.width <= 0 || ih.height == 0) {
    std::fclose(f);
    return false;
  }

  const int w = static_cast<int>(ih.width);
  const int h = static_cast<int>(std::abs(ih.height));
  const std::uint32_t row_stride = (static_cast<std::uint32_t>(w) * 3u + 3u) & ~3u;
  const std::size_t raw_size = static_cast<std::size_t>(row_stride) * static_cast<std::size_t>(h);
  std::vector<std::uint8_t> raw(raw_size);
  if (fh.off_bits > sizeof(fh) + sizeof(ih)) {
    const long skip = static_cast<long>(fh.off_bits - sizeof(fh) - sizeof(ih));
    if (skip < 0 || std::fseek(f, skip, SEEK_CUR) != 0) {
      std::fclose(f);
      return false;
    }
  }
  if (!read_all(f, raw.data(), raw.size())) {
    std::fclose(f);
    return false;
  }
  std::fclose(f);

  std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w * h * 4));
  const bool top_down = ih.height < 0;
  for (int y = 0; y < h; ++y) {
    const int src_y = top_down ? y : (h - 1 - y);
    const std::uint8_t* row = raw.data() + static_cast<std::size_t>(src_y) * row_stride;
    for (int x = 0; x < w; ++x) {
      const std::size_t di = static_cast<std::size_t>((y * w + x) * 4);
      const std::size_t si = static_cast<std::size_t>(x * 3);
      const std::uint8_t rr = row[si + 2];
      const std::uint8_t gg = row[si + 1];
      const std::uint8_t bb = row[si + 0];
      if (white_key && rr > 245 && gg > 245 && bb > 245) {
        rgba[di + 0] = 255;
        rgba[di + 1] = 255;
        rgba[di + 2] = 255;
        rgba[di + 3] = 0;
      } else if (white_key) {
        // 黑线白底：纹理须为白+alpha，否则 GL_MODULATE 下黑色会把实体色乘成近黑。
        rgba[di + 0] = 255;
        rgba[di + 1] = 255;
        rgba[di + 2] = 255;
        rgba[di + 3] = 255;
      } else {
        rgba[di + 0] = rr;
        rgba[di + 1] = gg;
        rgba[di + 2] = bb;
        rgba[di + 3] = 255;
      }
    }
  }

  return upload_texture_rgba(rgba.data(), w, h, out);
}

}  // namespace

bool load_texture_bmp_rgb24(const char* path_utf8, Texture2DRgb& out) noexcept {
  return load_texture_bmp_rgb24_impl(path_utf8, out, false);
}

bool load_texture_bmp_rgb24_white_key(const char* path_utf8, Texture2DRgb& out) noexcept {
  return load_texture_bmp_rgb24_impl(path_utf8, out, true);
}

void destroy_texture_2d(Texture2DRgb& tex) noexcept {
  if (tex.gl_name != 0) {
    GLuint n = static_cast<GLuint>(tex.gl_name);
    glDeleteTextures(1, &n);
  }
  tex = Texture2DRgb{};
}

}  // namespace cw::render
