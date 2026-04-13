#include "cw/situation_view/situation_view_chrome.hpp"

#if defined(_WIN32)
#include "cw/situation_view/situation_view_chrome_win32.hpp"
#endif

namespace cw::situation_view {

std::unique_ptr<SituationViewChrome> create_situation_view_chrome() {
#if defined(_WIN32)
  return std::make_unique<Win32SituationChrome>();
#else
  return std::make_unique<SituationViewChromeNull>();
#endif
}

}  // namespace cw::situation_view
