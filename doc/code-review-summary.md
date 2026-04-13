# 代码与工程评审摘要

本文档记录对当前仓库（C++ 引擎、想定解析、渲染与 Ninja 构建）的评审结论与改进建议，便于迭代时对照。详细背景见 [development-plan.md](development-plan.md)。**架构与设计层面的条目化补充**见本文 **§「架构与设计评审补充」**；[architecture.md](architecture.md) 第 7 节有简要索引。

---

## 已做得较好的方面

- `lib` 分层、引擎状态机、单机快照语义与联邦占位、自研想定 `.cws` 管线、Win32 OpenGL 显示通路与阶段规划基本一致。
- `EntityRecord` 已拆为装配 / 运动学与按模型类型的缓存（`entity_record.hpp`）；传感器调度顺序与文档一致（`Sensor` 在聚合末尾）。
- 想定解析支持 `ParseDiagnostics`、语料 `scenarios/corpus/` 与 `engine_tests` 回归；构建以 Ninja 为主并拆分 `ninja/*.ninja`。
- 数值侧有 `situation_digest` 金样与文档化的时钟/步进约定（[architecture.md](architecture.md) §3.6、[api.md](api.md) §2.3）。

---

## 不足与改进建议（按优先级大致排序）

### 1. 测试与质量门禁

- `cmd/verify/verify.cpp` 仍偏「能链接 + 基础数学」，业务覆盖有限。
- **建议**：以 `engine_tests` 为主力回归；重要行为变更时扩充语料或金样；需要时再引入轻量 CI。

### 2. 错误模型与可观测性（已部分落实）

- 已有 `error_code_str` / `error_message`、`log_error` 及扩展错误码。若要在调用点附带**可变长上下文**，可考虑 `Result` 或 out 参数式 `message`。

### 3. 引擎内部结构：`EntityRecord`（已部分落实）

- 装配、运动学与各模型缓存已分离；阶段 6 继续在子结构中扩展，避免单结构无限膨胀。

### 4. 模型调度与 `Sensor` 时机（已落实）

- `kModelPassOrder` 不含 `Sensor`；`aggregate_situation()` 末尾调用 `run_model_pass(Sensor)`，与「mover 之后、写入快照前刷新探测」一致。

### 5. 重复工具与常量（已部分落实）

- `cw::ieq`、`kPi`/`kPiF` 等已集中；渲染等 TU 中残留局部常量可随改动收敛。

### 6. 构建与可移植性（已部分落实）

- Ninja 模块化、`depfile`、`NINJA_FILE` 与 `build-headless.ninja` 已落地。**本项目不采用 CMake**（见历史结论）。

### 7. 数值与确定性（已部分落实）

- 时钟 `double`、步内 `float` 已文档化；`situation_digest` + 金样回归。**跨工具链**仍可能漂移，金样以约定工具链为准。

### 8. 想定解析器健壮性（已部分落实）

- 语料、`ParseDiagnostics::line`、重复 id / 通信数值 / `comm_link` 顺序等已在解析期收紧。**后续**：解析子码、转义/续行若纳入语法再扩语料（与下文 **A8** 呼应）。

### 9. 文档与产品表述（已落实）

- [README.md](../README.md) 含「当前实现范围与路线图」：**Lua 暂缓**，**可视化蓝图不暂缓**（阶段 7），与 [development-plan.md](development-plan.md) 一致。

### 10. 性能与规模（已部分落实）

- `compute_sensor_detections`：实体数较多时用 **x–y 均匀网格**粗筛（格宽与本步全局最大 `range_m` 相关），否则枚举。**局限**见下文 **A6**。

---

## 架构与设计评审补充（待迭代）

以下来自对当前实现的通读，**非已实现承诺**，供架构演进排期；编号 **A*** 与上文 **1–10** 独立。

| 编号 | 主题 | 问题或风险 | 优化 / 演进方向 |
|------|------|------------|-----------------|
| **A1** | 模块依赖（**已落实**） | ~~`ModelMountDesc` 曾通过 `cw::engine::types.hpp` 依赖引擎层。~~ | `ModelKind` 置于共享头 [`lib/core/cw/model_kind.hpp`](../lib/core/cw/model_kind.hpp)（`cw::ModelKind`）；`cw::engine::ModelKind` 为别名，想定解析仅依赖 `lib/core`。 |
| **A2** | `Engine` 组织 | `lib/engine/engine.cpp` 单文件承担生命周期、快照、想定映射、mover、传感器、`param_*` 等，**过长、难测、难并行修改**。 | 按域拆 TU（如 `engine_sensor.cpp`、`mount_params.cpp`）并配局部单测。 |
| **A3** | `aggregate_situation` | `initialize` / `start` / `pause` / `end` / `set_time_scale` / `apply_scenario` / `restore` / `add_entity` 等均触发**全量实体 → 快照拷贝**；实体上千时，仅调倍速也付全量拷贝成本。 | 区分轻量更新（只改 `sim_time`/`state`/`time_scale`）与完整聚合；或脏标记/按需填充。 |
| **A4** | 快照与传感器 | `SituationSnapshot` 同时是对外 DTO 与**传感器写入 `sensor_detections` 的工作区**，职责叠合。 | 内部先写 `SensorFrame` 再并入快照，便于以后双缓冲或异步化。 |
| **A5** | 热路径 | `has_model`/`find_mount` 线性扫挂载；mover 内 **`param_str` 可能每步分配** `std::string`。 | 初始化阶段缓存常用参数；或对每类模型缓存 `ModelMountDesc*`（需定义多挂载同类型语义）。 |
| **A6** | 传感器 broadphase | 格宽取**全局最大** `range_m` 时，单个远距传感器会**拉大 cell**，短程传感器邻域扫描仍偏大；网格键用 `int` 打包在极端坐标下有理论溢出风险（战术尺度通常可忽略）。 | 按射程分档或多层网格；文档化 2D 桶 + 3D 距离假设。 |
| **A7** | 想定 `comm_link` | 解析期要求 **`comm_node` 先于 `comm_link`**，对生成式/拼接想定不友好。 | 两遍扫描或读毕后统一建链，同时保留行号诊断能力。 |
| **A8** | 解析错误 | 仅 `ParseError` + 行号，**无稳定子码**，工具难以分类（重复 id、越界、顺序等）。 | `ParseDiagnostics` 增加枚举阶段/子码（整数即可）。 |
| **A9** | 快照检查点 | `save_snapshot` **深拷贝**实体与世界数据，规模大时内存与时间尖峰。 | 分层快照、可选字段或版本块；文档写明适用规模。 |
| **A10** | 运行程序边界 | `situation_view` 墙钟驱动 `step()` 与渲染**同线程**，壳层直接持 `Engine&`，与「只消费态势」的边界模糊。 | 抽象只读 `SituationSnapshot` + 输入队列，便于 headless、录制、多视口。 |
| **A11** | 蓝图与实现 | 文档约定**蓝图不暂缓**，引擎侧对 `entity_script` **仍以存储为主**，运行时未执行蓝图逻辑。 | 在架构/计划与代码注释中显式写清**排期 vs 现状**，避免读者误判能力已就绪。 |
| **A12** | `verify` 与 `engine_tests` | 两套「验收」入口并存，`verify` 价值偏低时易造成**门禁分散**。 | 以 `engine_tests` 为单一事实来源，或收缩 `verify` 职责并写进 [development.md](development.md)。 |

---

## 相关文档

- [index.md](index.md)
- [development-plan.md](development-plan.md)
- [architecture.md](architecture.md)（§7 与本节呼应）
- [development.md](development.md)
- [scenario-format.md](scenario-format.md)
