#pragma once

#include "cw/render/texture_bmp.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>

namespace cw::situation_view {

/// 按逻辑路径缓存实体2D 图标纹理（SVG 等），供战术图绘制使用。
struct IconTextureCache {
  std::unordered_map<std::string, cw::render::Texture2DRgb> by_logical_path;

  void destroy_all();

  cw::render::Texture2DRgb* get_or_load(const std::string& icon_2d_path);
};

}  // namespace cw::situation_view
