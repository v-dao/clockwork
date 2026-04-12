#include "cw/error.hpp"
#include "cw/log.hpp"
#include "cw/vec3.hpp"

#include <cstdlib>

int main() {
  using cw::Error;
  using cw::LogLevel;
  using cw::math::Vec3;

  cw::log(LogLevel::Info, "smoke: Clockwork phase-0 build OK");

  const Vec3 a{1.F, 0.F, 0.F};
  const Vec3 b{0.F, 1.F, 0.F};
  if (cw::math::length(a + b) <= 0.F) {
    cw::log(LogLevel::Error, "smoke: unexpected math");
    return EXIT_FAILURE;
  }

  if (!cw::ok(Error::Ok)) {
    cw::log(LogLevel::Error, "smoke: error helper broken");
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
