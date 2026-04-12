#include "cw/vec3.hpp"

namespace cw::math {

float dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

float length(Vec3 a) noexcept { return std::sqrt(dot(a, a)); }

}  // namespace cw::math
