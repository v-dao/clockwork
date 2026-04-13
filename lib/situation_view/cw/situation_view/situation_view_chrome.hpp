#pragma once

#include <memory>

namespace cw::render {
class GlWindow;
}
namespace cw::engine {
class Engine;
}
namespace cw::scenario {
struct Scenario;
}

namespace cw::situation_view {

class SituationViewShell;

/// 运行程序「壳层」中与 OS 相关的 UI（菜单栏、后续可扩展状态栏等），与 `SituationViewShell` 内的视图状态解耦。
class SituationViewChrome {
 public:
  virtual ~SituationViewChrome() = default;

  virtual void install_view_menu(cw::render::GlWindow& win, SituationViewShell& shell) = 0;
  virtual void install_simulation_menu(cw::render::GlWindow& win, SituationViewShell& shell) = 0;
  virtual void set_simulation_targets(cw::engine::Engine* engine,
                                      const cw::scenario::Scenario* scenario) noexcept = 0;
  virtual void sync_simulation_menu_from_engine() = 0;
};

/// 无原生菜单时的空实现（如未接入 GUI 的 Linux 构建占位）。
class SituationViewChromeNull final : public SituationViewChrome {
 public:
  void install_view_menu(cw::render::GlWindow&, SituationViewShell&) override {}
  void install_simulation_menu(cw::render::GlWindow&, SituationViewShell&) override {}
  void set_simulation_targets(cw::engine::Engine*, const cw::scenario::Scenario*) noexcept override {}
  void sync_simulation_menu_from_engine() override {}
};

[[nodiscard]] std::unique_ptr<SituationViewChrome> create_situation_view_chrome();

}  // namespace cw::situation_view
