#pragma once

#include "cw/error.hpp"
#include "cw/scenario/scenario.hpp"

#include <string_view>

namespace cw::scenario {

/// 从 UTF-8 文本解析想定（行格式见 parse.cpp 文件头注释）。
[[nodiscard]] Error parse_scenario_text(std::string_view text, Scenario& out);

/// 读取整个文件再解析（小想定；大文件后续可改流式）。
[[nodiscard]] Error parse_scenario_file(const char* path_utf8, Scenario& out);

}  // namespace cw::scenario
