#pragma once

#include "cw/scenario/scenario.hpp"

#include <string>

namespace cw::engine::detail {

/// 挂载参数键名 ASCII 比较不区分大小写；未找到返回空串。
[[nodiscard]] std::string param_str(const cw::scenario::ModelMountDesc& mount, const char* key);

[[nodiscard]] float param_float(const cw::scenario::ModelMountDesc& mount, const char* key, float fallback);

}  // namespace cw::engine::detail
