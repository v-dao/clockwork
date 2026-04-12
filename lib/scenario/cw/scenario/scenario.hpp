#pragma once

#include "cw/engine/types.hpp"
#include "cw/vec3.hpp"

#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cw::scenario {

/// 挂载模型及键值参数（对齐 api「列表及参数」）。
struct ModelMountDesc {
  cw::engine::ModelKind kind{};
  std::vector<std::pair<std::string, std::string>> params;
};

/// 控制逻辑绑定（Lua / 蓝图资源引用）。
struct ScriptBindingDesc {
  enum class Kind : std::uint8_t { Lua, Blueprint };
  Kind kind = Kind::Lua;
  std::string resource_path;
  std::string entry_symbol;  // Lua 入口；蓝图可空
};

/// 实体 + 平台与型号字段（对齐 api 第 3 节最大集）。
struct ScenarioEntityDesc {
  std::string external_id;  // 想定侧唯一标识，可空
  std::string name;
  std::string faction;
  std::string variant_ref;
  std::string icon_2d_path;
  std::string model_3d_path;
  /// 若 `has_display_color` 为真，态势显示用 RGB（0..1）；否则由阵营推断。
  bool has_display_color = false;
  float display_color_r = 1.F;
  float display_color_g = 1.F;
  float display_color_b = 1.F;
  std::vector<std::pair<std::string, std::string>> platform_attributes;
  cw::math::Vec3 position{};
  /// 机体坐标系速度 (m/s)：x 机头、y 右、z 下；见 `lib/ecs` 与 `entity_att`。
  cw::math::Vec3 velocity{};
  /// 度；偏航从北顺时针、俯仰抬头正、滚转右翼下偏正。未写则 0。
  float yaw_deg = 0.F;
  float pitch_deg = 0.F;
  float roll_deg = 0.F;
  std::vector<ModelMountDesc> mounts;
  std::optional<ScriptBindingDesc> script;
};

struct RouteWaypoint {
  float x = 0.F;
  float y = 0.F;
  float z = 0.F;
};

struct ScenarioRoute {
  std::string id;
  std::string display_name;
  std::vector<RouteWaypoint> waypoints;
  /// 若 `has_line_color` 为真，态势航线用 RGB（0..1）；否则为默认灰。
  bool has_line_color = false;
  float line_r = 0.55F;
  float line_g = 0.55F;
  float line_b = 0.6F;
  /// 若 `has_line_width` 为真，使用 `line_width_px`（OpenGL 线宽，像素）；否则 1.5。
  bool has_line_width = false;
  float line_width_px = 1.5F;
};

enum class AirspaceKind : std::uint8_t { Box, Polygon };

struct ScenarioAirspace {
  std::string id;
  AirspaceKind kind = AirspaceKind::Box;
  cw::math::Vec3 box_min{};
  cw::math::Vec3 box_max{};
  std::vector<cw::math::Vec3> polygon;
  std::vector<std::pair<std::string, std::string>> attrs;
};

struct CommNodeDesc {
  std::string id;
  std::string bound_entity;  // 可空：不绑定实体
  double bandwidth_bps = 0.0;
  double latency_ms = 0.0;
};

struct CommLinkDesc {
  std::string from_node;
  std::string to_node;
  double packet_loss = 0.0;  // 0..1
  double delay_ms = 0.0;
};

/// 想定内存表示；`version` 1=初版实体行，2=与最大字段集解析器同步（语法兼容，仅作演进标记）。
struct Scenario {
  int version = 0;
  std::vector<ScenarioEntityDesc> entities;
  std::vector<ScenarioRoute> routes;
  std::vector<ScenarioAirspace> airspaces;
  std::vector<CommNodeDesc> comm_nodes;
  std::vector<CommLinkDesc> comm_links;
};

}  // namespace cw::scenario
