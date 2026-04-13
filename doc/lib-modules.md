# Clockwork — `lib` 模块实现概要

本文档按**目录**概述 `lib/<库名>/` 的职责、主要类型与文件分工，便于导航代码与评审依赖边界。细节契约仍以 [api.md](api.md)、[architecture.md](architecture.md) 为准；想定语法见 [scenario-format.md](scenario-format.md)。

**建议依赖顺序（自底向上）**：`core` → `math` → `ecs` / `scenario` → `motion` → `engine` → `render` → `situation_view`。

---

## `lib/core`

| 内容 | 说明 |
|------|------|
| `cw/error.hpp`、`error.cpp` | `cw::Error` 错误码、`cw::ok` / `cw::error_code_str` / `cw::error_message`；供引擎、想定解析等返回。 |
| `cw/log.hpp`、`log.cpp` | `cw::log` / `cw::log_error` 分级日志。 |
| `cw/assert.hpp` | 断言宏（轻量）。 |
| `cw/string_match.hpp` | `cw::ieq` / `cw::ieq_cstr` 等 ASCII 大小写无关比较。 |
| `cw/model_kind.hpp` | `cw::ModelKind` 枚举：想定挂载与引擎调度共用的模型类型标识。 |

---

## `lib/math`

| 内容 | 说明 |
|------|------|
| `cw/vec3.hpp`、`vec3.cpp` | 三维向量与常用运算。 |
| `cw/mat4.hpp`、`linalg.cpp` | 4×4 矩阵与线性代数辅助。 |
| `cw/quat.hpp` | 四元数。 |
| `cw/camera_basis.hpp` | 相机基向量等几何辅助。 |
| `cw/rot_align.hpp` | 旋转对齐（如北向相关）。 |
| `cw/math/constants.hpp` | π 等常量（与 `std::numbers` 对齐的封装）。 |

---

## `lib/ecs`

| 内容 | 说明 |
|------|------|
| `cw/ecs/entity_coordinate_system.hpp`、`entity_coordinate_system.cpp` | 实体坐标系/姿态等与 ECS 相关的最小实现，供运动与引擎侧使用。 |

---

## `lib/motion`

| 内容 | 说明 |
|------|------|
| `cw/motion/motion_model.hpp` | 运动模型概念接口。 |
| `cw/motion/motion_model_3dof.hpp`、`motion_model_3dof.cpp` | 三自由度运动学 MVP：步进更新实体位姿/速度等，供 `Engine` 的 mover 路径调用。 |

---

## `lib/engine`

| 内容 | 说明 |
|------|------|
| `cw/engine/engine.hpp`、`engine.cpp` | **仿真引擎**：状态机（初始化/开始/暂停/结束）、固定步长、倍速、`step()`、单机快照 save/restore、`apply_scenario` / `reset_with_scenario`、`add_entity`；按模型类型调度 `run_model_pass`，聚合 `SituationSnapshot`；传感器粗筛与精判（见 [architecture.md](architecture.md) §3.6）。 |
| `cw/engine/types.hpp`、`types.cpp` | `EngineState` 等引擎侧枚举与轻量类型。 |
| `cw/engine/situation.hpp` | `SituationSnapshot`、`SituationPresentation`：态势只读视图与快照结构。 |
| `cw/engine/entity_record.hpp` | `EntityRecord` 及各类模型挂载缓存（签名、mover、通信、武器等）。 |
| `engine_mount.cpp` | 想定挂载到运行时实体的装配逻辑。 |
| `engine_mover.cpp` | 每步 mover 与航线/运动学更新。 |
| `engine_sensor.cpp` | 探测列表计算、空间哈希粗筛等。 |
| `situation_digest.cpp` | 对快照做规范序列化摘要（FNV-1a），供 `engine_tests` 确定性回归。 |

---

## `lib/scenario`

| 内容 | 说明 |
|------|------|
| `cw/scenario/scenario.hpp` | 想定内存结构：实体、平台、挂载参数、航线、空域、通信节点/链路、`entity_script` 等。 |
| `cw/scenario/parse.hpp`、`parse.cpp` | **`.cws` 文本想定解析**：版本校验、行级错误与 `ParseDiagnostics` / `ParseSubcode`；构建 `Scenario` 供 `Engine::apply_scenario` 消费。 |

---

## `lib/render`

| 内容 | 说明 |
|------|------|
| `cw/render/graphics_types.hpp` | `GraphicsApi` 等渲染无关枚举。 |
| `cw/render/graphics_device.hpp`、`graphics_device.cpp` | 图形设备抽象基类。 |
| `gl_graphics_device.hpp`、`gl_graphics_device.cpp` | OpenGL 默认帧缓冲呈现。 |
| `vulkan_graphics_device.hpp`、`vulkan_graphics_device.cpp` | Win32 Vulkan：交换链、清屏或 CPU BGRA 上传合成；可选「仅原生清屏」路径与每 swapchain 图像 fence 同步。 |
| `graphics_device_factory.cpp` | 按窗口 API 创建 `GraphicsDevice`。 |
| `cw/render/gl_window.hpp`、`gl_window.cpp`、`gl_window_factory.cpp` | 窗口抽象与工厂。 |
| `gl_window_win32.hpp`、`gl_window_win32.cpp` | Win32 窗口 + WGL；可选离屏 GL（Vulkan 合成路径）。 |
| `gl_offscreen_win32.hpp`、`gl_offscreen_win32.cpp` | 离屏 FBO、读回（含异步 PBO 路径）。 |
| `mercator_geo.hpp` | Web 墨卡托与经纬度互转等。 |
| `tactical_map_2d.hpp`、`tactical_map_2d.cpp` | 2D 战术图相机、视锥与绘制辅助。 |
| `globe_view_3d.hpp`、`globe_view_3d.cpp` | 3D 弧球相机、网格与标签锚点等。 |
| `lonlat_grid.hpp`、`lonlat_grid.cpp` | 经纬网生成与标签；与 `globe_pixel_scale.hpp` 共用标签像素比例上下限。 |
| `globe_program.hpp`、`globe_program.cpp` | GLSL 球面程序（可选）；与矢量陆块 atlas 等扩展相关。 |
| `texture_bmp.hpp`、`texture_bmp.cpp` | BMP 纹理加载。 |
| `svg_line_texture.hpp`、`svg_line_texture.cpp` | SVG 线条图标栅格化为纹理。 |
| `world_vector_lines.hpp`、`world_vector_lines.cpp` | `.mercl` 岸线/国界折线显示列表。 |
| `world_vector_merc.hpp`、`world_vector_merc.cpp` | `.merc2` 矢量陆块（GLU 剖分等）。 |
| `binary_stream_read.hpp` | 小端 `uint32` 二进制流读取（矢量文件格式共用）。 |

---

## `lib/situation_view`

| 内容 | 说明 |
|------|------|
| `cw/situation_view/situation_view_shell.hpp`、`situation_view_shell.cpp` | **视图壳层**：2D/3D/分屏模式、战术与地球相机、滚轮缩放、分屏比例尺同步、实体拾取与 `picked_entity_id`；与 `Engine::situation_presentation()` 协同。 |
| `cw/situation_view/situation_view_chrome.hpp`、`situation_view_chrome.cpp`、`situation_view_chrome_win32.cpp` | **宿主菜单 Chrome（Win32）**：视图模式、仿真暂停/恢复/结束/重置、倍速、图形 API 切换回调。 |
| `cw/situation_view/situation_map_globe_render.hpp`、`situation_map_globe_render.cpp` | **主绘制管线**：战术/地球/分屏 `draw_frame*`、实体与航线/空域/探测、HUD 位图字体叠加（FPS、仿真信息、实体简表）。 |
| `cw/situation_view/situation_hud.hpp` | HUD 数据结构。 |
| `cw/situation_view/asset_paths.hpp`、`asset_paths.cpp` | 想定目录-relative 资源搜索、`resolve_asset_path_utf8`、`append_relative_asset_candidates`；模板封装 `try_resolved_asset_candidates` / `try_append_asset_candidates`。 |
| `cw/situation_view/icon_texture_cache.hpp`、`icon_texture_cache.cpp` | 实体 2D 图标路径 → 纹理缓存（候选路径 + SVG 加载）。 |

---

## 相关文档

- [development.md](development.md) — 构建、`engine_tests`、`situation_view` 与 Vulkan 引导。
- [globe-3d-view.md](globe-3d-view.md) — 三维地球视图行为与实现核对。
- [index.md](index.md) — 文档索引。
