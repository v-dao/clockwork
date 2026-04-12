#include "cw/error.hpp"
#include "cw/log.hpp"
#include "cw/vec3.hpp"

#include <cstdlib>

/// Second entry point to prove the same static libs link from multiple cmd targets.
int main() {
  cw::log(cw::LogLevel::Info, "verify: second cmd links libcore + libmath");

  const cw::math::Vec3 v{3.F, 4.F, 0.F};
  if (cw::math::length(v) < 4.99F || cw::math::length(v) > 5.01F) {
    cw::log(cw::LogLevel::Error, "verify: length mismatch");
    return EXIT_FAILURE;
  }

  return cw::ok(cw::Error::InvalidArgument) ? EXIT_FAILURE : EXIT_SUCCESS;
}
