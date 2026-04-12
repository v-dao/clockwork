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

- **Ninja**：仓库根目录 [build.ninja](../build.ninja)，在根目录执行 `ninja`（或通过 `NINJA` 环境变量指定可执行文件路径）。默认 `exe_ext = .exe`（MinGW）；在 Linux / macOS 上把 `build.ninja` 顶部的 `exe_ext` 改为空即可得到无后缀二进制。
- **compile_commands.json**：默认目标会生成 `build/compile_commands.json`（供 clangd 等使用），由 [scripts/gen_compile_commands.py](../scripts/gen_compile_commands.py) 调用 `ninja -t compdb cxx` 并写入**仓库根目录绝对路径**的 `directory` 字段；需要 **Python 3** 在 `PATH` 中。若仅想手动导出，可执行：`ninja -t compdb cxx > build/compile_commands.json`（需自行处理 `directory` 路径是否满足工具要求）。
- **PowerShell 包装**：`powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1`（内部调用 `ninja`）。
- **验收**：`build/cmd/smoke/` 与 `build/cmd/verify/` 下生成可执行文件，二者链接同一组 `lib/core`、`lib/math` 目标文件，运行退出码为 0；且存在 `build/compile_commands.json`。
- **阶段 1**：`lib/engine` 提供仿真内核骨架；`cmd/engine_demo` 无 UI 驱动多步 `step()`，日志输出实体数量与仿真时间（默认 `ninja` 会链接并生成该程序）。
- **阶段 2**：`lib/scenario` 提供文本想定（`.cws`）解析，语法与字段见 `lib/scenario/parse.cpp` 文件头；支持 `version 1|2`，含实体/平台（`entity_id`/`entity_faction`/`entity_variant`/`entity_icon2d`/`entity_model3d`/`entity_attr`/`entity_mparam`）、`entity_script`、航线（`route`/`route_pt`）、空域（`airspace_box`/`airspace_poly`/`ap_vert`/`air_attr`）、通信（`comm_node`/`comm_link`）。`Engine::apply_scenario` 在 `initialize()` 后装载实体与世界静态数据；快照含航线/空域/通信副本。示例 `scenarios/minimal.cws` 与 `scenarios/full.cws`；`cmd/scenario_demo` 默认依次跑二者，传参则只跑该文件（路径含 `full` 时做最大集断言）。
- **阶段 4**：`lib/render` 提供最小 OpenGL 窗口封装（`cw/render/gl_window.hpp`，当前实现为 Windows Win32 + WGL）；**世界地图底图**为等距圆柱投影 BMP（`assets/maps/world_equirect_1024x512.bmp`，可由 `python scripts/gen_world_equirect_bmp.py` 生成），经 `cw/render/texture_bmp.hpp` 加载为纹理；`situation_view` 将仿真 **X–Y 平面坐标视为 Web 墨卡托米（EPSG:3857）** 采样底图 UV；底图四边形在墨卡托上至少扩至约 **±4000 km** 半宽再算 UV（战术视锥仍贴合实体），避免仅几十公里视域落在洋面时整屏单色。每帧绘制：底图、空域、航线、探测线、实体与速度矢量。默认想定 `scenarios/model_test.cws`，可传路径；**鼠标滚轮** 缩放（相对自动包围盒的视域）；**Home** 重置滚轮缩放（`zoom=1`）；**空格** 暂停/继续，**Esc** 退出。右下角经纬度与缩放信息在同一帧 OpenGL 内用 `wglUseFontBitmaps` 绘制（避免 SwapBuffers 后再用 GDI 与桌面合成导致闪烁）；`WM_ERASEBKGND` 直接返回 1 减少擦除背景与 GL 争抢。`build.ninja` 中 `link_gui` 链接 `-lgdi32 -lopengl32 -luser32`（MinGW）；非 Windows 需从 `default` 中移除 `situation_view` 或替换为平台相关链接。

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
