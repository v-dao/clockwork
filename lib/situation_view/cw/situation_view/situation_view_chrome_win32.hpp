#pragma once

#include "cw/situation_view/situation_view_chrome.hpp"

#include <functional>

namespace cw::situation_view {

class Win32SituationChrome final : public SituationViewChrome {
 public:
  void install_view_menu(cw::render::GlWindow& win, SituationViewShell& shell) override;
  void install_simulation_menu(cw::render::GlWindow& win, SituationViewShell& shell) override;
  void set_simulation_targets(cw::engine::Engine* engine,
                              const cw::scenario::Scenario* scenario) noexcept override;
  void sync_simulation_menu_from_engine() override;
  void set_graphics_api_switch_handler(std::function<void(cw::render::GraphicsApi)> fn) noexcept override;
  void sync_graphics_api_menu(cw::render::GraphicsApi current) noexcept override;

 private:
  static void menu_thunk(unsigned cmd, void* user);
  void on_menu_command(unsigned cmd);

  void* hwnd_main_ = nullptr;
  void* hmenu_view_ = nullptr;
  void* hmenu_sim_ = nullptr;
  void* hmenu_gfx_ = nullptr;
  SituationViewShell* shell_ = nullptr;
  cw::engine::Engine* engine_ = nullptr;
  const cw::scenario::Scenario* scenario_ = nullptr;
  std::function<void(cw::render::GraphicsApi)> gfx_api_handler_{};
};

}  // namespace cw::situation_view
