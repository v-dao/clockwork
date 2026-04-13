# 代码与工程评审摘要（浓缩版）

本文档是对仓库 C++ 引擎、想定解析、渲染与 Ninja 构建的**评审结论汇总**。细节与路线图见 [development-plan.md](development-plan.md)；架构索引见 [architecture.md](architecture.md) §7。

---

## 总体结论

分层（`lib`）、引擎状态机、单机快照与联邦占位、自研 `.cws`、Win32 OpenGL 与阶段规划**整体对齐**；`EntityRecord` 已按装配/运动学/模型缓存拆分；传感器在聚合末尾调度，与文档一致；想定侧有 `ParseDiagnostics`、语料与 `engine_tests`；数值有时钟/步进约定与 `situation_digest` 金样。

---

## 评审条目（原 1–10 与 A1–A12 融合）

下列按**主题**合并两条旧清单；**来源**列标出原编号（重复项只保留一行）。

| 主题 | 来源 | 结论要点 |
|------|------|----------|
| 测试与回归门禁 | 1 · A12 | `engine_tests` 为唯一回归可执行体；`verify` 已移除；[`build.ps1`](../scripts/build.ps1) 默认在 Ninja 成功后运行（`CLOCKWORK_SKIP_ENGINE_TESTS=1` 可跳过）。 |
| 错误模型与可观测 | 2 | 已有 `error_code_str` / `log_error` 等；若要在调用点附带可变长上下文，可演进 `Result` 或 out 参数式 `message`。 |
| 实体记录与挂载热路径 | 3 · A5 | `EntityRecord` 已拆装配/运动学/模型缓存，阶段 6 继续在子结构扩展；`has_model`/`find_mount` 仍线性扫挂载，mover 内 `param_str` 可能每步分配——可在初始化阶段缓存常用参数或挂载指针（需约定多挂载同类型语义）。 |
| 聚合、快照与传感器职责 | 4 · A3 · A4 | `Sensor` 在 `aggregate_situation()` 末尾执行，与文档一致；`start`/`pause`/`end`/`set_time_scale` 在实体未变时可 `patch_situation_meta()`，其余路径仍全量聚合（含传感器）；**演进**：内部可先写 `SensorFrame` 再并入快照，便于双缓冲或异步化。 |
| 重复工具与常量 | 5 | `cw::ieq`、`kPi`/`kPiF` 等已集中；渲染等 TU 中残留局部常量可随改动收敛。 |
| 构建与可移植性 | 6 | Ninja 模块化、`depfile`、`NINJA_FILE`、`build-headless.ninja` 已落地；**本项目不采用 CMake**。 |
| 数值与确定性 | 7 | 时钟 `double`、步内 `float` 已文档化；`situation_digest` 金样回归；跨工具链仍可能漂移，金样以约定工具链为准。 |
| 想定解析与通信语法 | 8 · A7 · A8 | `ParseDiagnostics`、`ParseSubcode`、`validate_scenario` 子码、重复 id / 通信数值等已收紧；**`comm_node` 须先于 `comm_link`** 为刻意单行流式规则（非缺陷），不计划两遍扫描；续行/转义若纳入语法再扩语料。 |
| 文档、路线图与蓝图现状 | 9 · A11 | README 与 development-plan 一致（Lua 暂缓，阶段 7 可视化蓝图不暂缓）；引擎对 `entity_script` 仍以存储为主，**须在架构/注释中写清排期与现状**，避免误判能力已就绪。 |
| 传感器性能与空间索引 | 10 · A6 | `compute_sensor_detections` 已用 x–y 均匀网格粗筛；**局限**：格宽取全局最大 `range_m` 时远距传感器会拉大 cell，短程邻域扫描仍偏大；网格键 `int` 在极端坐标下有理论溢出风险（战术尺度通常可忽略）；可按射程分档或多层网格并文档化假设。 |
| 模块边界与 Engine 组织 | A1 · A2 | **A1 已落实**：`ModelKind` 在 [`lib/core/cw/model_kind.hpp`](../lib/core/cw/model_kind.hpp)，想定解析不依赖引擎层。**A2 部分落实**：`Engine` 已按域拆为 `engine.cpp`、`engine_mount.cpp`、`engine_mover.cpp`、`engine_sensor.cpp` 等；可选针对 `detail::param_*` 或传感器 broadphase 的专项单测。 |
| 态势呈现与运行程序边界 | A10 | **部分落实**：`SituationPresentation` + `Engine::situation_presentation()`；`lib/render` 与 `situation_view` 的绘制/拾取/分屏同步以只读态势为输入。**仍**同线程墙钟 `step()`；**可选后续**：仿真命令队列、headless 多视口、跨线程投递快照。 |
| 快照检查点成本 | A9 | `save_snapshot` 深拷贝实体与世界数据，规模大时内存与时间尖峰；可演进分层快照、可选字段或版本块，并文档写明适用规模。 |

---

## 相关文档

- [index.md](index.md)
- [development-plan.md](development-plan.md)
- [architecture.md](architecture.md)
- [development.md](development.md)
- [scenario-format.md](scenario-format.md)
