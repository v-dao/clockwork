#pragma once

#include "cw/error.hpp"
#include "cw/scenario/scenario.hpp"

#include <cstdint>
#include <string_view>

namespace cw::scenario {

/// 想定解析错误子码（稳定整数值，供工具与测试分类；与 `cw::Error::ParseError` 配合使用）。
enum class ParseSubcode : std::uint16_t {
  None = 0,

  MissingVersion = 1,
  UnsupportedVersion = 2,
  NoEntities = 3,

  DuplicateRouteId = 10,
  EmptyRouteId = 11,
  DuplicateAirspaceId = 12,
  EmptyAirspaceId = 13,
  DuplicateCommNodeId = 14,
  EmptyCommNodeId = 15,
  CommLinkUnknownEndpoint = 16,
  PolygonTooFewVertices = 17,
  CommNodeUnknownBoundEntity = 18,

  UnknownCommand = 30,
  LineSyntaxError = 31,
  ExpectedNumber = 32,
  InvalidColor = 33,
  UnknownModelKind = 34,
  UnknownScriptBindingKind = 35,
  DuplicateEntityName = 36,
  EmptyEntityName = 37,
  EntityMissingModels = 38,
  UnknownEntityName = 39,
  UnknownRouteId = 40,
  UnknownAirspaceId = 41,
  MountKindNotFound = 42,
  RouteAttrUnknownKey = 43,
  KeyValueSyntaxError = 44,
  CommNumericOutOfRange = 45,
  WrongAirspaceKindForVertex = 46,
};

/// 解析失败时可选写入行号（1-based，与源文本物理行一致）与 **ParseSubcode**。
/// 重复 `route` / 空域 / `comm_node` id、`comm_node` 绑定不存在的实体等多在**当前行**报错；
/// 收尾 `validate_scenario` 失败为 **`line == 0`** 且子码区分结构问题。成功解析后 **`line == 0`** 且 **`subcode == None`**。
struct ParseDiagnostics {
  int line = 0;
  ParseSubcode subcode = ParseSubcode::None;
};

/// 从 UTF-8 文本解析想定（行格式见 parse.cpp 文件头注释）。
[[nodiscard]] Error parse_scenario_text(std::string_view text, Scenario& out,
                                        ParseDiagnostics* diag = nullptr);

/// 读取整个文件再解析（小想定；大文件后续可改流式）。
[[nodiscard]] Error parse_scenario_file(const char* path_utf8, Scenario& out,
                                        ParseDiagnostics* diag = nullptr);

}  // namespace cw::scenario
