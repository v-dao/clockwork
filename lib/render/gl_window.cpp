#include "cw/render/gl_window.hpp"

namespace cw::render {

GlWindow::~GlWindow() = default;

void GlWindow::destroy_hud_bitmap_font_lists(unsigned, int) noexcept {}

GlWindowHotkeyEdges GlWindow::poll_hotkey_edges() noexcept { return {}; }

}  // namespace cw::render
