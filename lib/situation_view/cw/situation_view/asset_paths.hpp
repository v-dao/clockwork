#pragma once

#include <initializer_list>
#include <optional>
#include <string>
#include <utility>
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

/// 对每个相对路径调用 `resolve_asset_path_utf8`，再对解析结果调用 `try_load(p)`，直到成功。
/// 成功时返回所用的解析路径；否则 `std::nullopt`。
template <typename Fn>
[[nodiscard]] std::optional<std::string> try_resolved_asset_candidates(std::initializer_list<const char*> rels,
                                                                       Fn&& try_load) {
  for (const char* rel : rels) {
    const std::string p = resolve_asset_path_utf8(rel);
    if (std::forward<Fn>(try_load)(p)) {
      return p;
    }
  }
  return std::nullopt;
}

/// 将 `rel_utf8` 展开为候选路径列表（与 `append_relative_asset_candidates` 相同），依次调用 `try_load(p)`。
template <typename Fn>
[[nodiscard]] bool try_append_asset_candidates(const std::string& rel_utf8, Fn&& try_load) {
  std::vector<std::string> cands;
  append_relative_asset_candidates(rel_utf8, cands);
  for (const auto& p : cands) {
    if (std::forward<Fn>(try_load)(p)) {
      return true;
    }
  }
  return false;
}

template <typename Fn>
[[nodiscard]] bool try_append_asset_candidates(const char* rel_utf8, Fn&& try_load) {
  return try_append_asset_candidates(std::string(rel_utf8 != nullptr ? rel_utf8 : ""), std::forward<Fn>(try_load));
}

}  // namespace cw::situation_view
