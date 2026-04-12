#!/usr/bin/env python3
"""One-shot: extract draw_* from situation_view.cpp into lib situation_map_globe_render.cpp."""
from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "cmd/situation_view/situation_view.cpp"
OUT = ROOT / "lib/situation_view/situation_map_globe_render.cpp"

HEADER = r"""#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include "cw/situation_view/situation_map_globe_render.hpp"

#include "cw/engine/engine.hpp"
#include "cw/render/globe_program.hpp"
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
#include <unordered_map>
#include <vector>

namespace cw::situation_view {

using cw::render::mercator_meters_to_lonlat;

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

"""

FOOTER = "\n}  // namespace cw::situation_view\n"


def strip_path_and_icon_loader_helpers(text: str) -> str:
    """Remove helpers now in asset_paths / icon_texture_cache (if present in extracted body)."""
    start_mark = "bool ends_with_icase(const std::string& s, const char* suf) {"
    end_mark = "void entity_display_rgb(const cw::engine::EntitySituation& e, float& r, float& g, float& b) {"
    i = text.find(start_mark)
    j = text.find(end_mark)
    if i < 0 or j < 0 or j <= i:
        return text
    return text[:i] + text[j:]


def remove_c_struct(text: str, struct_name: str) -> str:
    key = f"struct {struct_name} {{"
    i = text.find(key)
    if i < 0:
        return text
    brace = text.find("{", i)
    if brace < 0:
        return text
    depth = 0
    k = brace
    while k < len(text):
        c = text[k]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                semi = text.find(";", k)
                if semi < 0:
                    return text
                return text[:i] + text[semi + 1 :]
        k += 1
    return text


def main() -> int:
    src_text = SRC.read_text(encoding="utf-8")
    if "void draw_ocean_quad" not in src_text:
        print("Skip:", SRC, "has no draw_ocean_quad (maintain lib/situation_view/situation_map_globe_render.cpp by hand).")
        return 0
    lines = src_text.splitlines()
    start = next(i for i, l in enumerate(lines) if l.startswith("void draw_ocean_quad"))
    end = next(i for i, l in enumerate(lines) if i > start and l.strip() == "}  // namespace")
    body = "\n".join(lines[start:end])
    body = remove_c_struct(body, "IconTextureCache")
    body = remove_c_struct(body, "SituationHud")
    body = strip_path_and_icon_loader_helpers(body)

    text = HEADER + body + FOOTER

    text = text.replace("cw::situation_view::SituationViewShell& shell", "SituationViewShell& shell")
    text = text.replace(
        "void draw_frame_tactical(const cw::engine::Engine& eng, SituationViewShell& shell",
        "void draw_frame_tactical(const cw::engine::Engine& eng, SituationViewShell& shell",
    )

    old_uv = (
        "void draw_world_underlay_vector(const cw::render::MercatorOrthoFrustum& f,\n"
        "                                const cw::render::WorldVectorMerc* wv) {"
    )
    new_uv = (
        "void draw_world_underlay_vector(const cw::render::MercatorOrthoFrustum& f,\n"
        "                                const cw::render::WorldVectorMerc* wv, bool show_land) {"
    )
    text = text.replace(old_uv, new_uv)
    text = text.replace(
        "  draw_ocean_quad(f);\n  if (k_show_land_basemap && wv != nullptr && wv->valid()) {",
        "  draw_ocean_quad(f);\n  if (show_land && wv != nullptr && wv->valid()) {",
    )
    text = text.replace("k_show_land_basemap", "opts.show_land_basemap")

    text = re.sub(
        r"(void draw_frame_tactical\([^\)]*bool clear_color_buffer)\)",
        r"\1, const SituationRenderOptions& opts)",
        text,
        count=1,
    )
    text = re.sub(
        r"(void draw_frame_globe\([^\)]*bool clear_buffers)\)",
        r"\1, const SituationRenderOptions& opts)",
        text,
        count=1,
    )
    text = re.sub(
        r"(void draw_frame_split\([^\)]*GLuint hud_font_base)\)",
        r"\1, const SituationRenderOptions& opts)",
        text,
        count=1,
    )
    text = re.sub(
        r"(void draw_frame\([^\)]*GLuint hud_font_base)\)",
        r"\1, const SituationRenderOptions& opts)",
        text,
        count=1,
    )

    text = text.replace(
        "draw_world_underlay_vector(basemap, world_vec);",
        "draw_world_underlay_vector(basemap, world_vec, opts.show_land_basemap);",
    )

    text = text.replace(
        "draw_frame_tactical(eng, shell, split_x, vp_h, t_mx, t_my, world_vec, world_tex_gl, coastlines, boundary_lines,\n"
        "                      icon_cache, draw_simulation_layers, hud_out, hud_font_base, false);",
        "draw_frame_tactical(eng, shell, split_x, vp_h, t_mx, t_my, world_vec, world_tex_gl, coastlines, boundary_lines,\n"
        "                      icon_cache, draw_simulation_layers, hud_out, hud_font_base, false, opts);",
    )
    text = text.replace(
        "draw_frame_globe(eng, shell, right_w, vp_h, g_mx, g_my, world_vec, world_tex_gl, coastlines, boundary_lines,\n"
        "                   icon_cache, draw_simulation_layers, hud_out, hud_font_base, false);",
        "draw_frame_globe(eng, shell, right_w, vp_h, split_x, g_mx, g_my, world_vec, world_tex_gl, coastlines, boundary_lines,\n"
        "                   icon_cache, draw_simulation_layers, hud_out, hud_font_base, false, opts);",
    )
    text = text.replace(
        "draw_frame_globe(eng, shell, vp_w, vp_h, cursor_mx, cursor_my, world_vec, world_tex_gl, coastlines,\n"
        "                      boundary_lines, icon_cache, draw_simulation_layers, hud_out, hud_font_base, true);",
        "draw_frame_globe(eng, shell, vp_w, vp_h, vp_w, cursor_mx, cursor_my, world_vec, world_tex_gl, coastlines,\n"
        "                      boundary_lines, icon_cache, draw_simulation_layers, hud_out, hud_font_base, true, opts);",
    )
    text = text.replace(
        "draw_frame_globe(eng, shell, right_w, vp_h, g_mx, g_my, world_vec, world_tex_gl, coastlines, boundary_lines,\n"
        "                   icon_cache, draw_simulation_layers, hud_out, hud_font_base, false, opts);",
        "draw_frame_globe(eng, shell, right_w, vp_h, split_x, g_mx, g_my, world_vec, world_tex_gl, coastlines, boundary_lines,\n"
        "                   icon_cache, draw_simulation_layers, hud_out, hud_font_base, false, opts);",
    )
    text = text.replace(
        "draw_frame_globe(eng, shell, vp_w, vp_h, cursor_mx, cursor_my, world_vec, world_tex_gl, coastlines,\n"
        "                      boundary_lines, icon_cache, draw_simulation_layers, hud_out, hud_font_base, true, opts);",
        "draw_frame_globe(eng, shell, vp_w, vp_h, vp_w, cursor_mx, cursor_my, world_vec, world_tex_gl, coastlines,\n"
        "                      boundary_lines, icon_cache, draw_simulation_layers, hud_out, hud_font_base, true, opts);",
    )
    text = text.replace(
        "draw_frame_split(eng, shell, vp_w, vp_h, cursor_mx, cursor_my, world_vec, world_tex_gl, coastlines,\n"
        "                      boundary_lines, icon_cache, draw_simulation_layers, hud_out, hud_font_base);",
        "draw_frame_split(eng, shell, vp_w, vp_h, cursor_mx, cursor_my, world_vec, world_tex_gl, coastlines,\n"
        "                      boundary_lines, icon_cache, draw_simulation_layers, hud_out, hud_font_base, opts);",
    )
    text = text.replace(
        "draw_frame_tactical(eng, shell, vp_w, vp_h, cursor_mx, cursor_my, world_vec, world_tex_gl, coastlines,\n"
        "                      boundary_lines, icon_cache, draw_simulation_layers, hud_out, hud_font_base, true);",
        "draw_frame_tactical(eng, shell, vp_w, vp_h, cursor_mx, cursor_my, world_vec, world_tex_gl, coastlines,\n"
        "                      boundary_lines, icon_cache, draw_simulation_layers, hud_out, hud_font_base, true, opts);",
    )

    text = text.replace("  using cw::situation_view::ViewMode;\n", "")

    OUT.write_text(text, encoding="utf-8")
    print("Wrote", OUT, "lines", len(text.splitlines()))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
