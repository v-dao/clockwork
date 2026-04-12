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

---

## 4. 相关文件清单

- `lib/render/cw/render/globe_view_3d.hpp` — `GlobeEarthView` 对外 API
- `lib/render/globe_view_3d.cpp` — 投影、弧球、经纬网
- `cmd/situation_view/situation_view.cpp` — 主循环：左键拖动入队、滚轮缩放、3D 绘制入口
- `lib/render/globe_program.cpp` — GLSL 地球绘制（读取当前 OpenGL MVP）

---

## 5. 需求核对

| 需求 | 结论 |
|------|------|
| 屏幕北向与 `gluLookAt` 一致 | **满足**：显式滚转对齐 `n_p` 与 `u_cam`。 |
| 弧球粘点 `Rd*u0=u1`（仅最短弧步） | **单步**满足；北向滚转后略有偏差。 |

---

## 6. 修订记录

- 文档初稿：归纳「北朝上 + 左键拖动粘点」需求与实现。
- **北向修正**：近似的 swing–twist 不足以对齐屏幕；改为最短弧 + **`north_roll_align_content_R`**（与 `gluLookAt` 相机上对齐）。
- **弧球与拾取**：弧球增量须 **右乘** `content_R * Rd`；`u0,u1` 为模型系。射线–球面交点用 `content_R` 将命中点变到世界系后再与 `eye` 判半球；文档与实现对齐。
