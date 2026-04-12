#include "cw/vec3.hpp"

namespace cw::math {

float dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

float length(Vec3 a) noexcept { return std::sqrt(dot(a, a)); }

Vec3 scale(Vec3 a, float s) noexcept { return {a.x * s, a.y * s, a.z * s}; }

Vec3 normalize(Vec3 a, float epsilon) noexcept {
  const float L = length(a);
  if (L < epsilon) {
    return {1.F, 0.F, 0.F};
  }
  return scale(a, 1.F / L);
}

}  // namespace cw::math
