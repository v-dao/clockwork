#pragma once

#include "cw/engine/types.hpp"
#include "cw/scenario/scenario.hpp"
#include "cw/vec3.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cw::engine {

/// 来自想定或 `add_entity` 的静态装配：标识、显示、挂载与脚本引用（步进中通常只读）。
struct EntityAssembly {
  std::string external_id;
  std::string name;
  std::string faction;
  std::string variant_ref;
  std::string icon_2d_path;
  std::string model_3d_path;
  bool has_display_color = false;
  float display_color_r = 1.F;
  float display_color_g = 1.F;
  float display_color_b = 1.F;
  std::vector<std::pair<std::string, std::string>> platform_attributes;
  std::vector<cw::scenario::ModelMountDesc> mounts;
  std::optional<cw::scenario::ScriptBindingDesc> script;
};

/// 仿真推进可读写的位姿与运动学量。
struct EntityKinematics {
  cw::math::Vec3 position{};
  cw::math::Vec3 velocity{};
  cw::math::Vec3 angular_velocity{};
  float yaw_deg = 0.F;
  float pitch_deg = 0.F;
  float roll_deg = 0.F;
};

/// 从 `signature` 挂载派生，供 sensor 等消费（与装配、运动学分离）。
struct EntitySignatureCache {
  float radar_rcs_m2 = 10.F;
};

/// 从 `mover` 挂载派生，供运动模型与航线跟随使用。
struct EntityMoverCache {
  /// `route=<route_id>`，空表示不跟航线。
  std::string route_id;
  std::size_t route_wp_index = 0;
  /// `kind` / `pattern`，如 `3dof`、`stub`。
  std::string kind;
};

/// 从 `comdevice` 挂载派生；阶段 6 扩展队列与拓扑（当前仅解析常用占位参数）。
struct EntityComdeviceCache {
  /// 可选：`node_id=<id>`，与想定 `comm_node` 对齐。
  std::string bound_node_id;
};

/// 从 `weapon` 挂载派生；阶段 6 扩展射界与发射事件（当前仅解析占位参数）。
struct EntityWeaponCache {
  /// `rounds` / `magazine` 非负整数，缺省为 0。
  std::size_t rounds_ready = 0;
};

/// 引擎内一条实体的完整运行时记录（身份 + 装配 + 运动学 + 按模型类型的挂载缓存）。
struct EntityRecord {
  EntityId id = 0;
  EntityAssembly assembly;
  EntityKinematics kin;
  EntitySignatureCache signature_cache;
  EntityMoverCache mover_cache;
  EntityComdeviceCache comdevice_cache;
  EntityWeaponCache weapon_cache;
};

}  // namespace cw::engine
