#pragma once

#include "cw/error.hpp"
#include "cw/scenario/scenario.hpp"

#include <string_view>

namespace cw::scenario {

/// 解析失败时可选写入行号（1-based，与源文本物理行一致）。重复 `route` / 空域 / `comm_node` id、
/// `comm_node` 绑定不存在的实体等多在**当前行**报错；仅收尾结构校验（如多边形顶点数、通信链路节点）
/// 仍可能为 **`line == 0`**。成功解析后 **`line == 0`**。
struct ParseDiagnostics {
  int line = 0;
};

/// 从 UTF-8 文本解析想定（行格式见 parse.cpp 文件头注释）。
[[nodiscard]] Error parse_scenario_text(std::string_view text, Scenario& out,
                                        ParseDiagnostics* diag = nullptr);

/// 读取整个文件再解析（小想定；大文件后续可改流式）。
[[nodiscard]] Error parse_scenario_file(const char* path_utf8, Scenario& out,
                                        ParseDiagnostics* diag = nullptr);

}  // namespace cw::scenario
