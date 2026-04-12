#pragma once

namespace cw::math {

/// 列主序 4×4，左上角为将单位向量 `u` 旋转到 `v` 的旋转（Rodrigues）。
void rot_align_unit_vectors_to_mat4_col(const double u[3], const double v[3], double out_R[16]);

}  // namespace cw::math
