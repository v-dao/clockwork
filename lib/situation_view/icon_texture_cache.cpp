#include "cw/situation_view/icon_texture_cache.hpp"

#include "cw/situation_view/asset_paths.hpp"
#include "cw/log.hpp"
#include "cw/render/svg_line_texture.hpp"

#include <cctype>
#include <cstring>
#include <unordered_set>

namespace cw::situation_view {

namespace {

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

bool try_load_icon_texture(const std::string& path, cw::render::Texture2DRgb& out) {
  if (ends_with_icase(path, ".svg")) {
    return cw::render::load_svg_line_icon_texture(path.c_str(), out);
  }
  return false;
}

}  // namespace

void IconTextureCache::destroy_all() {
  for (auto& kv : by_logical_path) {
    cw::render::destroy_texture_2d(kv.second);
  }
  by_logical_path.clear();
}

cw::render::Texture2DRgb* IconTextureCache::get_or_load(const std::string& icon_2d_path) {
  const std::string key = icon_2d_path.empty() ? std::string("<default>") : icon_2d_path;
  const auto it = by_logical_path.find(key);
  if (it != by_logical_path.end()) {
    return &it->second;
  }
  cw::render::Texture2DRgb tex{};
  bool ok = false;
  if (!icon_2d_path.empty()) {
    ok = try_append_asset_candidates(icon_2d_path, [&](const std::string& p) {
      return try_load_icon_texture(p, tex);
    });
  }
  if (!ok) {
    ok = try_append_asset_candidates("assets/icons/AirPlane.svg", [&](const std::string& p) {
      return try_load_icon_texture(p, tex);
    });
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

}  // namespace cw::situation_view
