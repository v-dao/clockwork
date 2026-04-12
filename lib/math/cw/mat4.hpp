#pragma once

namespace cw::math {

/// OpenGL 列主序 4×4：`out = a * b`（列向量 `b` 的列先作用）。
template <typename T>
void mat4_mul_col_major(const T* a, const T* b, T* o) {
  for (int c = 0; c < 4; ++c) {
    for (int r = 0; r < 4; ++r) {
      o[c * 4 + r] = a[0 * 4 + r] * b[c * 4 + 0] + a[1 * 4 + r] * b[c * 4 + 1] + a[2 * 4 + r] * b[c * 4 + 2] +
                     a[3 * 4 + r] * b[c * 4 + 3];
    }
  }
}

}  // namespace cw::math
