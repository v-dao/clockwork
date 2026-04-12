#pragma once

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#endif
#include <GL/gl.h>

namespace cw::render {

struct WorldVectorMerc;

/// 在已有 OpenGL 上下文、且已请求 3.3 Compatibility 时初始化 GLSL 球面程序；失败则 `globe_program_ready()` 为 false。
[[nodiscard]] bool globe_program_try_init() noexcept;

void globe_program_shutdown() noexcept;
[[nodiscard]] bool globe_program_ready() noexcept;

/// 将矢量陆块栅格化到墨卡托纹理（与2D `draw_land_fill` 同一投影），供球面片元按经纬度反算 UV 采样。须在 `globe_program_try_init` 成功且 `wv.valid()` 后调用。
[[nodiscard]] bool globe_merc_atlas_build_from_vector_land(const WorldVectorMerc& wv) noexcept;
[[nodiscard]] bool globe_merc_atlas_valid() noexcept;

/// 使用当前矩阵栈的 P×MV 绘制半径 1 的单位球。`tex_gl` 为 equirect（可为 0）；`use_land_basemap` 为 true 且已构建墨卡托陆块图时优先采样陆栅格，否则仅用洋面色 / equirect（当为 true 且 `tex_gl` 非 0）。为 false 时强制纯色洋面（忽略陆栅格与纹理）。`enable_lighting` 为 false 时底图亮度均匀（无漫反射）。
void draw_globe_sphere_glsl(GLuint tex_gl, const float ocean_rgb[3], bool enable_lighting,
                            bool use_land_basemap) noexcept;

}  // namespace cw::render
