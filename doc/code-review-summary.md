# 代码与工程评审摘要

本文档记录对当前仓库（C++ 引擎、想定解析、渲染与 `build.ninja`）的评审结论与改进建议，便于迭代时对照。详细背景见 [development-plan.md](development-plan.md)。

---

## 已做得较好的方面

- `lib` 分层、引擎状态机、单机快照语义与联邦占位、自研想定管线、Win32 OpenGL 显示通路等与阶段规划基本一致，结构清晰。

---

## 不足与改进建议（按优先级大致排序）

### 1. 测试与质量门禁偏弱

- `cmd/verify/verify.cpp` 几乎只验证「能链接 + `Vec3::length`」，对业务价值有限；行为校验分散在 `engine_demo`、`scenario_demo` 等可执行程序中，缺少统一自动化回归。
- **建议**：为引擎状态机、快照、想定解析关键路径增加可脚本化的退出码测试（自研极简 test runner 或在 `main` 中断言 + 非零退出）；本地以 `ninja` 构建并运行 `engine_tests` 等程序做回归。

### 2. 错误模型偏粗、可观测性不足（已部分落实）

- 已扩展 `WrongState`、`NotAllowedWhenFederated`、`UnsupportedScenarioVersion`，并提供 `error_code_str` / `error_message`、`log_error`；引擎失败路径会记录操作与状态。若仍需逐次调用的可变长说明，可再引入 `Result` 或 out 参数式 `message`。

### 3. 引擎内部结构：`EntityRecord` 职责过重（已部分落实）

- 已拆为 `EntityAssembly`、`EntityKinematics`，以及按模型类型的缓存：`EntitySignatureCache`、`EntityMoverCache`、`EntityComdeviceCache`（`node_id` 等）、`EntityWeaponCache`（`rounds` / `magazine`等）；见 `lib/engine/cw/engine/entity_record.hpp`。阶段 6 可在对应子结构中扩展字段与步进逻辑，而不向装配或运动学堆叠。

### 4. 模型调度与代码可读性（已落实）

- `run_model_pass(Sensor)` 内调用 `compute_sensor_detections()`；`kModelPassOrder`不含 `Sensor`，由 `aggregate_situation()` 末尾统一调用 `run_model_pass(Sensor)`，与「mover 之后、写入快照前刷新探测」一致，见 `engine.cpp` 中调度注释。

### 5. 重复工具函数与魔法常量（已部分落实）

- `cw::ieq` / `cw::ieq_cstr` 见 `lib/core/cw/string_match.hpp`；`cw::math::kPi` / `kPiF`（`std::numbers`）见 `lib/math/cw/math/constants.hpp`。`parse.cpp`、`engine.cpp`、`motion_model_3dof.cpp`、`svg_line_texture.cpp` 及 `mercator_geo.hpp`、`entity_coordinate_system.cpp` 等已切换。其余渲染 TU 中局部 `kPi` 可随改动逐步收敛。

### 6. 构建与可移植性

- `build.ninja` 默认 GUI 链接为 Win32 + MinGW 库；非 Windows 需手工调整 `default`，对多平台协作不友好。
- 通过 `scenario_hpp` 手工钉依赖规避「只改头文件不重编」，规模扩大后易遗漏。
- **建议**：按平台拆分构建片段或条件规则；中长期可考虑 CMake + Ninja 生成依赖，或减少对单一「魔法头文件」的依赖声明。

### 7. 数值与确定性

- `sim_time_` / `fixed_dt_` 使用 `double`，大量几何与运动使用 `float`，长时间仿真累积与「确定性复现」目标需尽早约定。
- **建议**：文档化时间权威类型与步进公式；对关键路径做确定性回归（固定步数、比对状态摘要）。

### 8. 想定解析器健壮性

- `parse.cpp` 体量大、手写分词与状态，扩展语法时边界情况（转义、异常行、重复 id 等）风险高，且缺少系统化单测。
- **建议**：维护小型语料（有效/无效样例）与自动化测试；错误路径尽量返回 `ParseError` 并带行号（若尚未实现，对排错帮助大）。

### 9. 文档与产品表述对齐

- 根目录 [README.md](../README.md) 仍描述 Lua/蓝图等完整愿景，而 [development-plan.md](development-plan.md) 已说明 Lua 暂缓；易产生预期与代码不一致。
- **建议**：在 README 中增加简短「当前实现范围 / 路线图」，与实现和子文档对齐。

### 10. 性能与规模（MVP 可接受，需有扩展意识）

- 传感器双重循环为 O(n²)，实体规模很大时可能成为瓶颈；当前 MVP 可接受，但架构上可预留空间索引或分桶等扩展点。

---

## 相关文档

- [index.md](index.md)
- [development-plan.md](development-plan.md)
- [architecture.md](architecture.md)
- [development.md](development.md)
