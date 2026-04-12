# 三维地球视图：需求与实现说明

本文档归纳 `situation_view` 中 **3D globe** 模式的交互需求与当前代码中的对应功能，便于评审与后续改动对齐。

---

## 1. 需求（目标行为）与优先级

### 1.1 北朝上（**硬约束 / 必须满足**）

- **含义**：在固定 `gluLookAt(..., up = (0,1,0))` 的前提下，地理北（世界系 `(0,1,0)` 经 `content_R` 映射）在 **垂直于视线的平面** 上的投影，应与 **同一 `gluLookAt` 算出的相机「上」方向** 在该平面上的投影 **一致**，这样纬线相对屏幕水平、子午线竖直，无整体滚转感。
- **实现**：弧球先用最短弧 `Rd*u0=u1`，再 **左乘** 绕地心→相机轴 `normalize(eye)` 的滚转角 `θ`，使 `R*(0,1,0)` 的切向分量与 `glu_lookat_camera_up_world` 对齐（见 `north_roll_align_content_R`）。**不再**使用近似的 `quat_remove_twist_about_axis` 作为北向依据（该法不保证与 `gluLookAt` 的「上」一致）。

### 1.2 左键拖动「粘点」

- **含义**：最短弧满足 `Rd*u0=u1`（`u0,u1` 在地球模型系）；随后的 **北向滚转** 绕视线轴，会略微改变光标下命中点，故粘点为 **近似**（北向优先）。

---

## 2. 弧球算法（当前）

### 2.1 姿态更新顺序

`MODELVIEW = gluLookAt(eye→0, up=(0,1,0)) × content_R`。弧球增量 `Rd` 在 **地球模型系** 中将 `u0` 旋到 `u1`（`quat_from_two_unit_vectors` → 最短弧）。

- **须右乘**（与 `glMultMatrixd(content_R)` 的后乘一致）：  
  `R_tmp = content_R × Rd`，即 `R_new = R_old * Rd`。  
  若误写为 `Rd * R_old`，等价于在世界系左乘弧球，会出现竖拖反向、乱轴等问题。

### 2.2 北向滚转（在弧球之后）

`north_roll_align_content_R(R_tmp, eye)`：

- 取地理北 `n = R` 的第二列（`R*(0,1,0)`）；  
- 将 `n` 投影到垂直于 `v = normalize(eye)` 的平面得 `n_p`；  
- 用与 `gluLookAt(eye, 0,0,0, 0,1,0)` 相同公式得到相机上方向 `u_cam`（`glu_lookat_camera_up_world`）；  
- `θ = atan2((n_p×u_cam)·v, n_p·u_cam)`；  
- `R ← R_roll(v,θ) × R`（左乘绕视线轴的滚转）。

### 2.3 像素射线与单位球（`try_pixel_unit_world`）

- `gluUnProject` 在 **当前 MODELVIEW** 下得到的是 **地球模型系** 中的射线（单位球在原点）。  
- 与球面可能有两个正根 `t`；须优先取 **朝向相机一侧** 的交点：在世界系下 `dot(p_world, eye) > 0`。  
- 将射线上的点旋到世界系时只用 **`content_R` 的 3×3**：`p_world = R_content * p_body`，**不要**用完整 `MODELVIEW` 与 `eye` 做点积（否则混用相机系与世界系）。

**坐标系小结**：`u0,u1,Rd` 在 **模型系**；`eye`、`u_cam`、`p_world` 用于北向与半球判别时在 **世界系**（地心为原点、与 `compute_eye` 一致）。

---

## 3. 功能与代码位置

| 功能 | 说明 | 主要文件 / 符号 |
|------|------|------------------|
| 视图模式 | 2D 战术图 / 3D 地球切换 | `situation_view.cpp`：`ViewMode`，菜单 `View` |
| 投影与 MODELVIEW | 透视 + `lookAt(eye→0) × content_R` | `lib/render/globe_view_3d.cpp`：`setup_projection_and_modelview` |
| 轨道相机参数 | `yaw` / `pitch` / `distance`；默认拖动 **不** 改 yaw/pitch | `GlobeEarthView::GlobeCamera`；滚轮改 `distance` |
| 弧球拖动入队 | 左键按下且像素变化时排队一对像素 | `situation_view.cpp`：`queue_arcball_drag` |
| 弧球应用 | `process_pending_arcball`：`R_new = content_R * Rd` + **`north_roll_align_content_R`** | `globe_view_3d.cpp` |
| 取消挂起拖动 | 左键松开时清除未消费的弧球状态 | `clear_arcball_pending` |
| 重置姿态 | `HOME` / 切出 3D 等路径会重置 `content_R` 并清 pending | `reset_content_orientation` / `reset_globe_view_auxiliary_state` |
| 经纬拾取 | HUD 与光标下球面点；射线取 `dot(p_world,eye)>0` 的交点 | `try_pixel_lonlat` / `try_pixel_unit_world` |
| 经纬网步长 | 与2D 战术图共用 `pick_lonlat_step_deg`；最细 **0.05°** | `lib/render/lonlat_grid.cpp` |
| 经纬网绘制 | 局部带状 / 全球封顶、归并顶点、`POLYGON_OFFSET_LINE` | `draw_globe_lonlat_grid`、`globe_patch_diameter_deg` |

---

## 4. 相关文件清单

- `lib/render/cw/render/globe_view_3d.hpp` — `GlobeEarthView` 对外 API
- `lib/render/globe_view_3d.cpp` — 投影、弧球、经纬网绘制与 `globe_patch_diameter_deg`
- `lib/render/lonlat_grid.cpp` / `cw/render/lonlat_grid.hpp` — `pick_lonlat_step_deg`、`visible_sphere_diameter_deg`、注记格式化与 2D 战术经纬网
- `cmd/situation_view/situation_view.cpp` — 主循环：左键拖动入队、滚轮缩放、3D 绘制入口；`kGlobeGridR` 网格半径
- `lib/render/globe_program.cpp` — GLSL 地球绘制（读取当前 OpenGL MVP）

---

## 5. 需求核对

| 需求 | 结论 |
|------|------|
| 屏幕北向与 `gluLookAt` 一致 | **满足**：显式滚转对齐 `n_p` 与 `u_cam`。 |
| 弧球粘点 `Rd*u0=u1`（仅最短弧步） | **单步**满足；北向滚转后略有偏差。 |

---

## 6. 经纬网与注记（实现摘要）

### 6.1 步长（与 2D 共用）

- `pick_lonlat_step_deg(visible_diameter_deg, camera_distance)`（`lonlat_grid.cpp`）根据理想跨度、`h = d - 1` 及线数上限选出离散步长；候选序列最细为 **0.05°**（依次为0.05、0.1、0.25…），在 `max_mer_lines` / `max_par_lines` 约束下可能再加倍变粗。
- 3D 侧先用 **`visible_sphere_diameter_deg(camera_distance)`** 作为第一个参数，用于决定「这一缩放级别用多细的 `step`」，与相机距离一致。

### 6.2 绘制哪些经线 / 纬线

- **问题**：贴球时 `visible_sphere_diameter_deg` 可达近 180°，与 HUD 上「地面只有几十公里宽」不符；若仅按整球条数封顶（如 168 条经线），放大后屏幕上往往只剩一两根经线。
- **`globe_patch_diameter_deg(vp_w, vp_h, camera_distance)`**：与 `situation_view` 中 HUD 地面宽度同一套假设（单位球半径 1、`h_eye = d - 1`、竖直 FOV **50°**、视口纵横比），估算视口中心附近地面张角的 **较大边**（度）。
- **局部带状模式**：`viewport_center_valid && patch_deg < 92° && n_mer > 1` 时，只在中心经度索引带 `[i_draw_lo…i_draw_hi]` 与中心纬度索引带 `[k_draw_lo…k_draw_hi]` 内画线；带内 `mer_stride` / `par_stride` 由带宽、约 **patch/step/9** 的目标密度、以及经线 **160** / 纬线 **120** 的条数预算共同约束（取预算步长与密度步长的较小者）。
- **全球模式**：不满足上式时，经线约 **168** 条、纬线约 **84** 条封顶（整球均匀步进）。

### 6.3 折线质量与性能

- 每条经线 / 纬线为一条连续 **`GL_LINE_STRIP`**；背向半球依赖深度缓冲相对球面几何隐藏弦段，不按顶点切段。
- **T 型接缝**：均匀弧长上的纬度（或经度）与「当前实际绘制的纬线纬度 / 经线经度」做 **双指针归并**，交点处共顶点；每帧预计算 `par_lats_for_meridian_merge` 与 `mer_lons_draw`，**不对每条线做全表排序**。
- **`GL_POLYGON_OFFSET_LINE`** + `glPolygonOffset(0, -3)` 减轻线与球面及线–线交点的深度抖动。
- 网格画在略大于单位球的半径上：`situation_view.cpp` 中 **`kGlobeGridR = 1.00055`**（海岸线等略低）。

### 6.4 注记

- 相机较近（`camera_distance < 4.6`）且视口中心有效时：**十字注记**——过视口中心经线侧重纬度、过中心纬线侧重经度，中心格点合并为「纬 经」；实现见 `draw_globe_lonlat_grid` 与 HUD 光栅字绘制。
- `fmt_lon` / `fmt_lat`：当 `step_deg < 0.1°` 时经纬标签为 **两位小数**（0.05° 步长走此分支）。

---

## 7. 修订记录

- 文档初稿：归纳「北朝上 + 左键拖动粘点」需求与实现。
- **北向修正**：近似的 swing–twist 不足以对齐屏幕；改为最短弧 + **`north_roll_align_content_R`**（与 `gluLookAt` 相机上对齐）。
- **弧球与拾取**：弧球增量须 **右乘** `content_R * Rd`；`u0,u1` 为模型系。射线–球面交点用 `content_R` 将命中点变到世界系后再与 `eye` 判半球；文档与实现对齐。
- **经纬网**：补充 `pick_lonlat_step_deg` 最细 **0.05°**、`globe_patch_diameter_deg` 局部带状绘制、归并与线偏移；相关文件列入 §4。
