# Clockwork — 开发与工程约束

本文档汇总 README 中的**技术栈与仓库布局**约定，供实现与评审使用。

---

## 1. 语言与工具链

| 项 | 要求 |
|----|------|
| 语言 | C++ |
| 标准 | **C++20** |
| 编译器 | **g++** |

---

## 2. 仓库目录约定

| 路径 | 用途 |
|------|------|
| `cmd/<程序名称>/*.cpp` | 可执行程序源码 |
| `lib/<库名称>/*.cpp` | 接口库、动态库、静态库源码 |

程序与库的划分（例如引擎、显示、想定编辑器是否为多个 `cmd`）由项目演进决定，但须符合上述路径规则。

### 2.1 阶段 0 构建与验证

- **Ninja**：根目录 [build.ninja](../build.ninja) 为默认描述（含 Win32 `situation_view` 与 `exe_ext = .exe`，面向 MinGW）。**Linux / macOS / WSL** 等无 Win32 OpenGL 时使用 [build-headless.ninja](../build-headless.ninja)：`ninja -f build-headless.ninja`（`exe_ext` 为空，不编译 `gl_window_win32`、不链接 `situation_view`）。公共规则与对象列表在 [ninja/](ninja/) 下拆分（`rules.ninja`、`objects_common.ninja` 等），避免两套描述漂移。
- **头文件依赖**：`rule cxx` 使用 `-MMD -MF`与 Ninja `depfile`，由编译器生成 `.d` 文件跟踪 `#include`，**无需**再手工在 `build` 行钉 `scenario.hpp` 等。
- **compile_commands.json**：默认目标会生成 `build/compile_commands.json`，由 [scripts/gen_compile_commands.py](../scripts/gen_compile_commands.py) 调用 `ninja -f <NINJA_FILE> -t compdb cxx`；需要 **Python 3**。环境变量 **`NINJA_FILE`** 默认为 `build.ninja`；若日常只用 headless 构建，导出前请设置 `NINJA_FILE=build-headless.ninja`（Windows：`set NINJA_FILE=build-headless.ninja`），再 `ninja -f build-headless.ninja build/compile_commands.json`。手动导出：`ninja -f build.ninja -t compdb cxx > build/compile_commands.json`（需自行写入 `directory` 绝对路径时仍以脚本为准）。
- **PowerShell 包装**：`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1`（内部调用 `ninja`）。
- **阶段 0 快速验收**：`build/cmd/smoke/` 下可执行文件链接 `lib/core`、`lib/math`，用于确认基础库与第二 `cmd` 目标可链接；运行退出码为 0。默认 `ninja` 仍生成该目标。另须存在 `build/compile_commands.json`（见上节）。
- **回归门禁（单一事实来源）**：**`engine_tests`**（`cmd/engine_tests/engine_tests.cpp`）覆盖想定解析（含 `scenarios/full.cws`）、引擎状态机、单机快照与 `apply_scenario` 关键路径；另含 **确定性**用例：相同想定下分两段与一次性跑满固定步数，对 `cw::engine::situation_digest(snapshot)` 金样对拍（见 `lib/engine/situation_digest.cpp`）。失败时进程非零退出。默认 `ninja` 会生成 `build/cmd/engine_tests/engine_tests`（Windows 下常见为同路径 `.exe`）。**请在仓库根目录运行**，以便相对路径 `scenarios/` 与 `scenarios/full.cws` 有效。[`scripts/build.ps1`](../scripts/build.ps1) 在 Ninja 成功后**自动运行** `engine_tests`；仅编译、不跑测试时可设置环境变量 **`CLOCKWORK_SKIP_ENGINE_TESTS=1`**。亦可按需单独构建该目标（例如 `ninja build/cmd/engine_tests/engine_tests` 或带 `exe_ext` 的等价路径），不必先编 `situation_view`。
- **错误与日志**：`cw::error_code_str` / `cw::error_message`、`cw::log_error`；见 [api.md](api.md) 第 2.2 节。实现位于 `lib/core/error.cpp` 与 `log.cpp`。链接使用 `cw::log` 的目标时需同时链接 `error.o`。
- **公共工具**：ASCII 大小写不敏感比较 `cw::ieq` / `cw::ieq_cstr`（[cw/string_match.hpp](../lib/core/cw/string_match.hpp)）；π `cw::math::kPi` / `cw::math::kPiF`（[cw/math/constants.hpp](../lib/math/cw/math/constants.hpp)，C++20 `std::numbers`）。
- **阶段 1**：`lib/engine` 提供仿真内核骨架；运行时实体由 `cw/engine/entity_record.hpp` 中 `EntityAssembly`、`EntityKinematics` 与按模型类型的缓存（`EntitySignatureCache`、`EntityMoverCache`、`EntityComdeviceCache`、`EntityWeaponCache`）组成 `EntityRecord`。`cmd/engine_demo` 无 UI 驱动多步 `step()`，日志输出实体数量与仿真时间（默认 `ninja` 会链接并生成该程序）。
- **阶段 2**：`lib/scenario` 提供文本想定（`.cws`）解析，语法与字段见 `lib/scenario/parse.cpp` 文件头；支持 `version 1|2`，含实体/平台（`entity_id`/`entity_faction`/`entity_variant`/`entity_icon2d`/`entity_model3d`/`entity_attr`/`entity_mparam`）、`entity_script`、航线（`route`/`route_pt`）、空域（`airspace_box`/`airspace_poly`/`ap_vert`/`air_attr`）、通信（`comm_node`/`comm_link`）。`Engine::apply_scenario` 在 `initialize()` 后装载实体与世界静态数据；快照含航线/空域/通信副本。示例 `scenarios/minimal.cws` 与 `scenarios/full.cws`；`cmd/scenario_demo` 默认依次跑二者，传参则只跑该文件（路径含 `full` 时做最大集断言）。**解析排障**：可选 `cw::scenario::ParseDiagnostics` 取失败行号与 **`ParseSubcode`**（见 `cw/scenario/parse.hpp`）；重复的航线/空域/通信节点 id、`comm_node` 绑定无效实体、`comm_link` 先于未声明节点、通信数值越界等会在**对应行**报错（`comm_link` 须写在两端 `comm_node` 之后）。回归语料在 **`scenarios/corpus/`**（由 `engine_tests` 覆盖）。
- **阶段 4**：`lib/render` 提供最小 OpenGL 窗口封装（`cw/render/gl_window.hpp`，当前实现为 Windows Win32 + WGL）；**态势只读视图模型**：`cw::engine::SituationPresentation`（`cw/engine/situation.hpp`）聚合本帧 `SituationSnapshot` 与航线/空域引用，由 `Engine::situation_presentation()` 提供；战术墨卡托与 `lib/situation_view` 绘制/拾取/分屏视口同步均依赖该结构，**勿缓存其引用跨越** `step()`。**世界地图底图**为等距圆柱投影 BMP（`assets/maps/world_equirect_1024x512.bmp`，可由 `python scripts/gen_world_equirect_bmp.py` 生成），经 `cw/render/texture_bmp.hpp` 加载为纹理；`situation_view` 将仿真 **X–Y 平面坐标视为 Web 墨卡托米（EPSG:3857）** 采样底图 UV；底图四边形在墨卡托上至少扩至约 **±4000 km** 半宽再算 UV（战术视锥仍贴合实体），避免仅几十公里视域落在洋面时整屏单色。每帧绘制：底图、空域、航线、探测线、实体与速度矢量。默认想定 `scenarios/model_test.cws`，可传路径；**鼠标滚轮** 缩放（相对自动包围盒的视域）；**Home** 重置滚轮缩放（`zoom=1`）；**空格** 暂停/继续，**Esc** 退出。右下角经纬度与缩放信息在同一帧 OpenGL 内用 `wglUseFontBitmaps` 绘制（避免 SwapBuffers 后再用 GDI 与桌面合成导致闪烁）；`WM_ERASEBKGND` 直接返回 1 减少擦除背景与 GL 争抢。`ninja/rules.ninja` 中 `link_gui` 链接 `-lgdi32 -lopengl32 -luser32`（MinGW）；非 Windows 使用 `build-headless.ninja`，后续若增加 GLX 等可仿照 `targets_gui_win32.ninja` 增平台片段。

---

## 3. 依赖与渲染

| 约束 | 说明 |
|------|------|
|第三方依赖 | **不使用**第三方依赖库 |
| 界面 | 界面库需**封装界面类** |
| 图形 | 底层使用 **OpenGL** 渲染 |

自研解析、数学、容器等需在 `lib` 内实现或采用标准库；Lua 若作为脚本语言引入，是否算作「第三方」需在项目决策中明确（README 未禁止脚本运行时，仅禁止依赖库习惯上指不链接外部包管理器依赖；若团队将 Lua 视为内置源码则一致）。

---

## 4. 相关文档

- 需求：[requirements.md](requirements.md)
- 架构：[architecture.md](architecture.md)
- 接口契约：[api.md](api.md)
- 开发计划：[development-plan.md](development-plan.md)
