#pragma once

#include <cmath>
#include <cstddef>

namespace cw::math {

struct Vec3 {
  float x = 0.F;
  float y = 0.F;
  float z = 0.F;

  constexpr Vec3() noexcept = default;
  constexpr Vec3(float x_, float y_, float z_) noexcept : x(x_), y(y_), z(z_) {}

  [[nodiscard]] constexpr Vec3 operator+(Vec3 o) const noexcept {
    return {x + o.x, y + o.y, z + o.z};
  }
  [[nodiscard]] constexpr Vec3 operator-(Vec3 o) const noexcept {
    return {x - o.x, y - o.y, z - o.z};
  }
};

[[nodiscard]] float dot(Vec3 a, Vec3 b) noexcept;
[[nodiscard]] float length(Vec3 a) noexcept;

}  // namespace cw::math
