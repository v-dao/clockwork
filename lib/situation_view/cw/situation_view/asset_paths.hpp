#pragma once

#include <string>
#include <vector>

namespace cw::situation_view {

/// 设置想定文件路径后，以其**所在目录的上一级**作为资源搜索根（与原先 `g_scenario_file_dir` 语义一致）。
void set_scenario_directory_for_asset_search_utf8(const std::string& scenario_file_path_utf8);

/// 想定文件父目录的绝对规范化路径（UTF-8）。
std::string scenario_file_parent_absolute_utf8(const std::string& scen_path_utf8);

/// 在 cwd、`../`、`../../`、exe 上级目录及（若已设置）想定相对仓库根等路径下，找第一个可读文件。
std::string resolve_asset_path_utf8(const char* user_path_utf8);

/// 供图标等拼接候选路径：`root/rel`、`rel`、`../rel`…
void append_relative_asset_candidates(const std::string& rel_utf8, std::vector<std::string>& out);

}  // namespace cw::situation_view
