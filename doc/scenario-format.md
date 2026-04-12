# 想定文件格式（.cws）

Clockwork 使用自研文本想定，扩展名建议 `.cws`（Clockwork Scenario）。解析实现见 `lib/scenario/parse.cpp`；内存结构见 `lib/scenario/cw/scenario/scenario.hpp`。

## 通用规则

- **编码**：按解析器以字节流读取，建议使用 UTF-8；关键字与模型名比较为**大小写不敏感**。
- **分行**：每行一条指令；行首 `#` 为注释；仅含空白字符的行忽略。
- **分词**：以空白分隔 token；`entity_attr`、`entity_mparam`、`air_attr` 等命令中，**值**为从指定列起**整行剩余部分**（可含空格）。
- **版本**：文件中必须出现一行 `version 1` 或 `version 2`。语法相同，`2` 为演进标记；解析后写入 `Scenario::version`。
- **实体顺序**：`entity` 必须先于针对该实体名的扩展行（`entity_faction`、`entity_mparam` 等）。
- **航线**：必须先有 `route <id> ...`，再对该 `id` 使用 `route_pt` / `route_pt_geo`；可选 `route_attr` 设置显示颜色与线宽。
- **多边形空域**：必须先 `airspace_poly <id>`，再对该 `id` 使用 `ap_vert`（至少 3 个顶点方可通过校验）。
- **通信**：`comm_link` 引用的节点 id 必须已在 `comm_node` 中定义；`comm_node` 的 `entity <name>` 须为已声明的实体名。

## 模型种类（entity 行与 entity_mparam）

`entity` 行末尾可挂一个或多个模型，token 为：


| Token（不区分大小写） | 含义           |
| ------------- | ------------ |
| `mover`       | 运动模型         |
| `sensor`      | 传感器模型        |
| `comdevice`   | 通信设备         |
| `processor`   | 处理机          |
| `weapon`      | 武器           |
| `signature`   | 特征/签名（如 RCS） |


每条 `entity` 至少挂载一个模型。

## 指令参考

### 版本

```
version 1
```

或 `version 2`。

### 实体

**世界坐标（与 `situation_view` 一致）：位置为 Web 墨卡托米 (x,y) + 海拔米 (z)。**

**速度与姿态**：`entity_vel` 为 **机体坐标系（与 NED 轴一致）** 速度 (m/s)：**x** 北向机头、**y** 东向右翼、**z** 向下；`entity_att` 为 **NED 下** 的偏航/俯仰/滚转（度），**欧拉顺序 Z → Y → X**，再经 `lib/ecs` 转为局地东-北-天世界速度后写入引擎。详见 `cw/ecs/entity_coordinate_system.hpp`。

`entity` **只**声明实体名与挂载模型种类；**位置、机体系速度、姿态角默认为 0**，由 `entity_pos`、`entity_vel`、`entity_att` 设置（与 `entity_color` 相同风格，后写覆盖）：

```
entity <name> <model> [<model> ...]
entity_pos <name> geo <lon_deg> <lat_deg> <alt_m>
entity_pos <name> mercator|m|meters <mx> <my> <mz>
entity_pos <name> <mx> <my> <mz>
entity_vel <name> <vx> <vy> <vz>
entity_att <name> <yaw_deg> <pitch_deg> <roll_deg>
```

- `<name>` 在同一想定内唯一，且非空。
- `entity_pos` / `entity_vel` / `entity_att` 须在对应 `entity` 之后出现；同一实体多次书写时**后写覆盖先写**（与 `entity_color` 一致）。
- **偏航**：0° 朝北、90° 朝东（从北顺时针）；**俯仰**：0° 水平，+90° 朝天、−90° 朝地；**滚转**：沿机头 +X 轴，从机尾向机头看顺时针为正。
- 纬度超出 Web 墨卡托有效范围时会在解析阶段钳位（约 ±85.05°）。

### 实体扩展


| 指令     | 语法                                       | 说明                               |
| ------ | ---------------------------------------- | -------------------------------- |
| 位置     | `entity_pos <name> …`                    | `geo` / `mercator` / 三数字墨卡托米；见上文 |
| 速度     | `entity_vel <name> <vx> <vy> <vz>`       | 机体系 m/s（ECS）；未写则 0          |
| 姿态     | `entity_att <name> <yaw> <pitch> <roll>` | NED 角，度；顺序 Z,Y,X；未写则 0        |
| 外部 id  | `entity_id <name> <external_id>`         | 想定侧唯一标识                          |
| 阵营     | `entity_faction <name> <faction_token>`         | 单 token                          |
| 型号     | `entity_variant <name> <variant_ref>`           | 单 token                          |
| 二维图标   | `entity_icon2d <name> <path>`                   | 资源路径                             |
| 态势显示色  | `entity_color <name> …`                         | 见下文；未指定时由 `entity_faction` 推断           |
| 三维模型   | `entity_model3d <name> <path>`                  | 资源路径                             |
| 平台属性   | `entity_attr <name> <key> <value...>`           | value 为剩余整行                      |
| 挂载参数   | `entity_mparam <name> <model> <key> <value...>` | model 须为该实体已声明的挂载种类；value 为剩余整行  |
| 实体脚本   | `entity_script <name> <kind> <path> …`         | `kind` 区分类型（见下文）；后写覆盖先写 |


同一实体多次书写 `entity_script` 时**后写覆盖先写**。

**`entity_script` 的 `kind`（不区分大小写）**

| `kind` | 含义 | 附加参数 |
| ------ | ---- | -------- |
| `lua`、`lua_script` | Lua 资源 | 可选 `entry <sym>`（入口符号；缺省为空） |
| `blueprint`、`bp` | 蓝图 JSON 等资源 | 无 |

示例：

```
entity_script alpha lua logic/uav_main.lua entry on_tick
entity_script bravo blueprint bp/decoy.json
entity_script charlie bp bp/decoy.json
```

`**entity_color`（态势显示用 RGB，与引擎物理无关）** 任选一种写法：

- `entity_color <name> <r> <g> <b>` — 三个整数 **0～255**（与常见网页/CSS 一致）。
- `entity_color <name> #RRGGBB` 或 `entity_color <name> #RGB` — 十六进制，如 `#ff8800`、`#f80`。
- `entity_color <name> <颜色名>` — **英文**（不区分大小写，`-`/`_` 可省略）或 **中文单字**（如 `红`、`蓝`、`橙`；完整列表见 `lib/scenario/parse.cpp` 中 `lookup_named_color_rgb`）。英文名含 `red`、`cornflowerblue`、`orange`、`sky_blue` 等。

### 航线

```
route <route_id> <display_name_one_token>
route_pt <route_id> <mx> <my> <mz>
route_pt_geo <route_id> <lon_deg> <lat_deg> <alt_m>
route_attr <route_id> color <r> <g> <b>
route_attr <route_id> color <#RRGGBB|#RGB|颜色名>
route_attr <route_id> width <px>
```

- `route_pt`：墨卡托米 + 海拔米。
- `route_pt_geo`：经纬度 + 海拔，内部转为墨卡托米。
- `display_name` 当前解析为**单个 token**（若需空格应改用下划线或在展示层映射）。
- `route_attr … color`：航线在二维/三维态势中的线条颜色；写法与 **`entity_color`** 相同（`0～255` 三整数、十六进制或颜色名）。未写则默认灰。
- `route_attr … width`：线条宽度（OpenGL 线宽，单位像素），范围 **(0, 64]**。未写则默认 `1.5`。

### 空域

**轴对齐包围盒：**

```
airspace_box <id> <minx> <miny> <minz> <maxx> <maxy> <maxz>
airspace_box_geo <id> <min_lon> <min_lat> <min_alt> <max_lon> <max_lat> <max_alt>
```

- `airspace_box`：墨卡托米。
- `airspace_box_geo`：地理对角 SW(min_lon,min_lat,min_alt) 与 NE(max_lon,max_lat,max_alt) 转墨卡托；跨时区/大区域时仅为近似轴对齐盒。

**多边形（水平面常用）：**

```
airspace_poly <id>
ap_vert <id> <mx> <my> <mz>
ap_vert_geo <id> <lon_deg> <lat_deg> <alt_m>
...
air_attr <id> <key> <value...>
```

- `ap_vert`：墨卡托米；`ap_vert_geo`：经纬度 + 海拔。

### 通信网

```
comm_node <id> [entity <bound_name>] [bw <bps>] [lat_ms <ms>]
comm_link <from_id> <to_id> [loss <0..1>] [delay_ms <ms>]
```

- 可选键值须按 `键值` 成对出现，顺序任意。

## 引擎当前消费的挂载参数（阶段 3 MVP）

以下参数由 `entity_mparam` 写入对应挂载的 `params`，引擎在运行时读取（键名匹配为大小写不敏感）。未列出者由后续阶段或脚本消费。

### mover


| 键               | 类型  | 说明                                                 |
| --------------- | --- | -------------------------------------------------- |
| `max_speed_mps` | 浮点  | 最大速度（m/s），默认约 100                                  |
| `route`         | 字符串 | 航线 **id**（`route <route_id> …` 的第一个 token），**不是**展示名；缺省表示不跟航线，仅按初速积分 |


### sensor


| 键         | 类型  | 说明                                                 |
| --------- | --- | -------------------------------------------------- |
| `range_m` | 浮点  | 最大探测距离（m），默认 30000                                 |
| `fov_deg` | 浮点  | 视场角（度）；**≥ 359** 视为全向；否则相对速度方向的前向锥，半角为 `fov_deg/2` |


### signature


| 键        | 类型  | 说明                                 |
| -------- | --- | ---------------------------------- |
| `rcs_m2` | 浮点  | 雷达截面积（m²），默认 10，供 sensor 输出到态势探测列表 |


## 示例片段

最小实体与挂载参数：

```
version 2

entity alpha mover sensor
entity_pos alpha 0 0 1000
entity_mparam alpha sensor range_m 8000
entity_mparam alpha mover max_speed_mps 340

entity bravo signature
entity_pos bravo 500 0 0
entity_mparam bravo signature rcs_m2 12.5
```

完整示例见仓库 `scenarios/full.cws`；阶段 3 运动与探测示例见 `scenarios/model_test.cws`。

## 与态势显示（阶段 4）的坐标约定

`cmd/situation_view` 在 **X–Y 平面** 上绘制实体与想定图层时，将 `(position.x, position.y)` 解释为 **Web 墨卡托投影米**（与 EPSG:3857 一致：东向 x、北向 y，赤道海平面半径取 WGS84 长半轴）。世界底图为 **等距圆柱**纹理，由墨卡托米换算经纬度后再映射到纹理 UV。若想定使用局部切平面米而非全球墨卡托，底图仅作示意，需后续在想定或引擎中增加 CRS 元数据再精确对齐。

## 与代码的对应关系


| 概念   | 类型 / 字段                                              |
| ---- | ---------------------------------------------------- |
| 想定根  | `cw::scenario::Scenario`                             |
| 实体   | `ScenarioEntityDesc`（`mounts` 为 `ModelMountDesc` 列表） |
| 挂载参数 | `ModelMountDesc::params`，`pair<string,string>`       |
| 航线   | `ScenarioRoute`（`waypoints`）                         |


解析入口：`cw::scenario::parse_scenario_file(path, out)`、`parse_scenario_text(text, out)`（声明见 `cw/scenario/parse.hpp`）。