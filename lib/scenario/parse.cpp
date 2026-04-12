#include "cw/scenario/parse.hpp"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <array>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cw::scenario {

/*
 Clockwork 文本想定（阶段 2 最大集，自研）

通用：行首 # 注释；空行忽略。version 1 或 2（语法相同，2 为演进标记）。

  实体（必选至少一条）：
    entity <name> <model> [<model> ...]   # 位姿默认 0；用 entity_pos / entity_vel 设置
  航线/空域地理坐标（结果转为 Web 墨卡托米，与态势一致）：
    route_pt_geo <route_id> <lon°> <lat°> <alt_m>
    ap_vert_geo <id> <lon°> <lat°> <alt_m>
    airspace_box_geo <id> <min_lon> <min_lat> <min_alt> <max_lon> <max_lat> <max_alt>

  实体扩展（name 须已存在；后写覆盖先写，与 color 相同）：
    entity_pos <name> geo <lon°> <lat°> <alt_m>
    entity_pos <name> mercator|m|meters <mx> <my> <mz>
    entity_pos <name> <mx> <my> <mz>              # 墨卡托米 + 海拔米
    entity_vel <name> <vx> <vy> <vz>              # 世界系速度 m/s（默认 0）
    entity_id <name> <external_id>
    faction <name> <faction_token>
    variant <name> <variant_ref>
    icon2d <name> <path>
    color <name> <r> <g> <b>              # 0..255，态势显示实体色
    color <name> <#RRGGBB|#RGB>           # 十六进制，如 #ff8800 或 #f80
    color <name> <color_name>             # 英文名或中文名，如 orange、skyblue、橙
    model3d <name> <path>
    attr <name> <key> <value...>        # value 为剩余整行
    mparam <name> <model> <key> <value...>
    script_lua <name> <path> [entry <sym>]
    script_blueprint <name> <path>

  航线：
    route <route_id> <display_name_one_token>
    route_pt <route_id> <x> <y> <z>

  空域：
    airspace_box <id> <minx> <miny> <minz> <maxx> <maxy> <maxz>
    airspace_poly <id>
    ap_vert <id> <x> <y> <z>
    air_attr <id> <key> <value...>

  通信：
    comm_node <id> [entity <bound_name>] [bw <bps>] [lat_ms <ms>]
    comm_link <from_id> <to_id> [loss <0..1>] [delay_ms <ms>]
*/

namespace {

void trim_inplace(std::string& s) {
  while (!s.empty() &&
         (s.front() == '\r' || static_cast<unsigned char>(s.front()) <= ' ')) {
    s.erase(s.begin());
  }
  while (!s.empty() &&
         (s.back() == '\r' || static_cast<unsigned char>(s.back()) <= ' ')) {
    s.pop_back();
  }
}

std::vector<std::string> split_ws(const std::string& line) {
  std::vector<std::string> tok;
  std::string cur;
  for (char ch : line) {
    if (static_cast<unsigned char>(ch) <= ' ') {
      if (!cur.empty()) {
        tok.push_back(std::move(cur));
        cur.clear();
      }
    } else {
      cur.push_back(ch);
    }
  }
  if (!cur.empty()) {
    tok.push_back(std::move(cur));
  }
  return tok;
}

bool ieq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
      return false;
    }
  }
  return true;
}

bool parse_double(const std::string& s, double& out) {
  char* end = nullptr;
  out = std::strtod(s.c_str(), &end);
  return end != s.c_str() && end != nullptr && *end == '\0' && std::isfinite(out);
}

bool parse_u8_255(const std::string& s, unsigned& out) {
  char* end = nullptr;
  const unsigned long v = std::strtoul(s.c_str(), &end, 10);
  if (end == s.c_str() || end == nullptr || *end != '\0' || v > 255UL) {
    return false;
  }
  out = static_cast<unsigned>(v);
  return true;
}

bool parse_hex_digit(char c, unsigned& out) {
  if (c >= '0' && c <= '9') {
    out = static_cast<unsigned>(c - '0');
    return true;
  }
  if (c >= 'a' && c <= 'f') {
    out = static_cast<unsigned>(c - 'a' + 10);
    return true;
  }
  if (c >= 'A' && c <= 'F') {
    out = static_cast<unsigned>(c - 'A' + 10);
    return true;
  }
  return false;
}

bool parse_hex_byte_pair(char a, char b, unsigned& out) {
  unsigned hi = 0;
  unsigned lo = 0;
  if (!parse_hex_digit(a, hi) || !parse_hex_digit(b, lo)) {
    return false;
  }
  out = hi * 16U + lo;
  return true;
}

/// `#RGB` 或 `#RRGGBB`（大小写均可）。
bool parse_hex_color_token(const std::string& s, float& r, float& g, float& b) {
  if (s.size() == 4 && s[0] == '#') {
    unsigned h1 = 0;
    unsigned h2 = 0;
    unsigned h3 = 0;
    if (!parse_hex_digit(s[1], h1) || !parse_hex_digit(s[2], h2) || !parse_hex_digit(s[3], h3)) {
      return false;
    }
    const unsigned r8 = h1 * 17U;
    const unsigned g8 = h2 * 17U;
    const unsigned b8 = h3 * 17U;
    r = static_cast<float>(r8) / 255.F;
    g = static_cast<float>(g8) / 255.F;
    b = static_cast<float>(b8) / 255.F;
    return true;
  }
  if (s.size() == 7 && s[0] == '#') {
    unsigned r8 = 0;
    unsigned g8 = 0;
    unsigned b8 = 0;
    if (!parse_hex_byte_pair(s[1], s[2], r8) || !parse_hex_byte_pair(s[3], s[4], g8) ||
        !parse_hex_byte_pair(s[5], s[6], b8)) {
      return false;
    }
    r = static_cast<float>(r8) / 255.F;
    g = static_cast<float>(g8) / 255.F;
    b = static_cast<float>(b8) / 255.F;
    return true;
  }
  return false;
}

std::string normalize_color_name_key(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (char c : s) {
    if (c == '_' || c == '-') {
      continue;
    }
    o.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return o;
}

bool lookup_named_color_rgb(const std::string& spec, float& r, float& g, float& b) {
  static const std::unordered_map<std::string, std::array<std::uint8_t, 3>> kEnglish = {
      {"red", {255, 71, 64}},
      {"blue", {64, 140, 255}},
      {"green", {51, 242, 89}},
      {"yellow", {255, 217, 51}},
      {"gold", {255, 215, 0}},
      {"orange", {255, 140, 0}},
      {"cyan", {0, 206, 209}},
      {"magenta", {255, 0, 255}},
      {"purple", {138, 43, 226}},
      {"violet", {148, 0, 211}},
      {"pink", {255, 105, 180}},
      {"brown", {139, 69, 19}},
      {"white", {245, 245, 245}},
      {"black", {32, 32, 32}},
      {"gray", {166, 166, 178}},
      {"grey", {166, 166, 178}},
      {"silver", {192, 192, 200}},
      {"navy", {0, 64, 128}},
      {"lime", {50, 205, 50}},
      {"olive", {107, 142, 35}},
      {"teal", {0, 128, 128}},
      {"maroon", {128, 0, 0}},
      {"coral", {255, 127, 80}},
      {"salmon", {250, 128, 114}},
      {"cornflowerblue", {100, 149, 237}},
      {"deepskyblue", {0, 191, 255}},
      {"skyblue", {135, 206, 235}},
      {"turquoise", {64, 224, 208}},
      {"tan", {210, 180, 140}},
      {"khaki", {240, 230, 140}},
      {"neutral", {166, 166, 178}},
      {"crimson", {220, 20, 60}},
      {"darkred", {139, 0, 0}},
      {"darkgreen", {0, 100, 0}},
      {"darkblue", {0, 0, 139}},
      {"darkorange", {255, 140, 0}},
      {"lightblue", {173, 216, 230}},
      {"lightgreen", {144, 238, 144}},
      {"indigo", {75, 0, 130}},
      {"azure", {240, 255, 255}},
      {"beige", {245, 245, 220}},
      {"ivory", {255, 255, 240}},
      {"plum", {221, 160, 221}},
      {"orchid", {218, 112, 214}},
      {"tomato", {255, 99, 71}},
      {"wheat", {245, 222, 179}},
  };
  /// 中文颜色名（UTF-8），与英文表独立匹配。
  static const std::unordered_map<std::string, std::array<std::uint8_t, 3>> kZh = {
      {"\xe7\xba\xa2", {220, 20, 60}},   // 红
      {"\xe8\x93\x9d", {64, 140, 255}},  // 蓝
      {"\xe7\xbb\xbf", {51, 242, 89}},    // 绿
      {"\xe9\xbb\x84", {255, 217, 51}},   // 黄
      {"\xe6\xa9\x99", {255, 140, 0}},   // 橙
      {"\xe7\xb4\xab", {138, 43, 226}},   // 紫
      {"\xe9\x9d\x92", {0, 206, 209}},    // 青
      {"\xe7\x99\xbd", {245, 245, 245}}, // 白
      {"\xe9\xbb\x91", {32, 32, 32}},     // 黑
      {"\xe7\x81\xb0", {166, 166, 178}},  // 灰
      {"\xe7\xb2\x89", {255, 105, 180}}, // 粉
      {"\xe6\xa3\x95", {139, 69, 19}},    // 棕
  };

  auto apply = [](const std::array<std::uint8_t, 3>& rgb, float& rr, float& gg, float& bb) {
    rr = static_cast<float>(rgb[0]) / 255.F;
    gg = static_cast<float>(rgb[1]) / 255.F;
    bb = static_cast<float>(rgb[2]) / 255.F;
  };

  bool ascii_only = true;
  for (unsigned char uc : spec) {
    if (uc > 127U) {
      ascii_only = false;
      break;
    }
  }
  if (ascii_only) {
    const std::string k = normalize_color_name_key(spec);
    if (const auto it = kEnglish.find(k); it != kEnglish.end()) {
      apply(it->second, r, g, b);
      return true;
    }
  }
  if (const auto it = kZh.find(spec); it != kZh.end()) {
    apply(it->second, r, g, b);
    return true;
  }
  return false;
}

bool parse_color_token(const std::string& tok, float& r, float& g, float& b) {
  if (tok.empty()) {
    return false;
  }
  if (tok[0] == '#') {
    return parse_hex_color_token(tok, r, g, b);
  }
  return lookup_named_color_rgb(tok, r, g, b);
}

Error model_from_token(const std::string& t, cw::engine::ModelKind& out) {
  using cw::engine::ModelKind;
  if (ieq(t, "mover")) {
    out = ModelKind::Mover;
  } else if (ieq(t, "sensor")) {
    out = ModelKind::Sensor;
  } else if (ieq(t, "comdevice")) {
    out = ModelKind::Comdevice;
  } else if (ieq(t, "processor")) {
    out = ModelKind::Processor;
  } else if (ieq(t, "weapon")) {
    out = ModelKind::Weapon;
  } else if (ieq(t, "signature")) {
    out = ModelKind::Signature;
  } else {
    return Error::ParseError;
  }
  return Error::Ok;
}

ScenarioEntityDesc* find_entity(Scenario& sc, const std::string& name) {
  for (auto& e : sc.entities) {
    if (e.name == name) {
      return &e;
    }
  }
  return nullptr;
}

ScenarioAirspace* find_airspace(Scenario& sc, const std::string& id) {
  for (auto& a : sc.airspaces) {
    if (a.id == id) {
      return &a;
    }
  }
  return nullptr;
}

std::string join_tokens(const std::vector<std::string>& tok, std::size_t from) {
  std::string r;
  for (std::size_t i = from; i < tok.size(); ++i) {
    if (i > from) {
      r.push_back(' ');
    }
    r += tok[i];
  }
  return r;
}

/// 解析 comm_node / comm_link 行尾的可选 [key val] 对。
Error parse_kv_pairs(const std::vector<std::string>& tok, std::size_t start,
                     const std::unordered_map<std::string, int>& keys_one_float,
                     std::unordered_map<std::string, double>& out_float,
                     const std::unordered_map<std::string, int>& keys_one_string,
                     std::unordered_map<std::string, std::string>& out_str) {
  for (std::size_t i = start; i < tok.size();) {
    const std::string& k = tok[i];
    if (keys_one_string.count(k)) {
      if (i + 1 >= tok.size()) {
        return Error::ParseError;
      }
      out_str[k] = tok[i + 1];
      i += 2;
      continue;
    }
    if (keys_one_float.count(k)) {
      if (i + 1 >= tok.size()) {
        return Error::ParseError;
      }
      double v = 0;
      if (!parse_double(tok[i + 1], v)) {
        return Error::ParseError;
      }
      out_float[k] = v;
      i += 2;
      continue;
    }
    return Error::ParseError;
  }
  return Error::Ok;
}

/// Web 墨卡托（EPSG:3857）米；与态势显示 `situation_view` 一致。
void lon_lat_alt_to_mercator(double lon_deg, double lat_deg, double alt_m, float& out_x, float& out_y,
                             float& out_z) {
  constexpr double kR = 6378137.0;
  constexpr double kPi = 3.14159265358979323846;
  constexpr double kMaxLat = 85.0511287798066;
  const double lat_clamped = std::max(-kMaxLat, std::min(kMaxLat, lat_deg));
  const double lon_rad = lon_deg * (kPi / 180.0);
  const double lat_rad = lat_clamped * (kPi / 180.0);
  double x = kR * lon_rad;
  double y = kR * std::log(std::tan(kPi / 4.0 + lat_rad * 0.5));
  constexpr double kYMax = 20037508.34;
  y = std::max(-kYMax, std::min(kYMax, y));
  out_x = static_cast<float>(x);
  out_y = static_cast<float>(y);
  out_z = static_cast<float>(alt_m);
}

Error validate_scenario(const Scenario& sc) {
  std::unordered_set<std::string> nodes;
  for (const auto& n : sc.comm_nodes) {
    if (n.id.empty()) {
      return Error::ParseError;
    }
    nodes.insert(n.id);
  }
  for (const auto& l : sc.comm_links) {
    if (!nodes.count(l.from_node) || !nodes.count(l.to_node)) {
      return Error::ParseError;
    }
  }
  for (const auto& a : sc.airspaces) {
    if (a.kind == AirspaceKind::Polygon && a.polygon.size() < 3) {
      return Error::ParseError;
    }
  }
  std::unordered_set<std::string> ent_names;
  for (const auto& e : sc.entities) {
    ent_names.insert(e.name);
  }
  for (const auto& n : sc.comm_nodes) {
    if (!n.bound_entity.empty() && !ent_names.count(n.bound_entity)) {
      return Error::ParseError;
    }
  }
  return Error::Ok;
}

}  // namespace

Error parse_scenario_text(std::string_view text, Scenario& out) {
  out = Scenario{};
  int version = 0;

  std::string chunk(text);
  std::istringstream in(chunk);
  std::string line;
  while (std::getline(in, line)) {
    trim_inplace(line);
    if (line.empty()) {
      continue;
    }
    if (!line.empty() && line[0] == '#') {
      continue;
    }
    const auto tok = split_ws(line);
    if (tok.empty()) {
      continue;
    }
    const std::string& cmd = tok[0];

    if (ieq(cmd, "version")) {
      if (tok.size() < 2) {
        return Error::ParseError;
      }
      version = std::atoi(tok[1].c_str());
      if (version != 1 && version != 2) {
        return Error::ParseError;
      }
      continue;
    }

    if (ieq(cmd, "entity")) {
      if (tok.size() < 3) {
        return Error::ParseError;
      }
      ScenarioEntityDesc ent;
      ent.name = tok[1];
      if (ent.name.empty()) {
        return Error::ParseError;
      }
      /// position / velocity 默认 0；由 entity_pos、entity_vel 设置。
      for (std::size_t i = 2; i < tok.size(); ++i) {
        ModelMountDesc md;
        const Error me = model_from_token(tok[i], md.kind);
        if (!cw::ok(me)) {
          return me;
        }
        ent.mounts.push_back(std::move(md));
      }
      if (ent.mounts.empty()) {
        return Error::ParseError;
      }
      out.entities.push_back(std::move(ent));
      continue;
    }

    if (ieq(cmd, "entity_pos")) {
      if (tok.size() < 4) {
        return Error::ParseError;
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return Error::ParseError;
      }
      if (ieq(tok[2], "geo") || ieq(tok[2], "wgs84") || ieq(tok[2], "ll")) {
        if (tok.size() < 6) {
          return Error::ParseError;
        }
        double lon = 0;
        double lat = 0;
        double alt = 0;
        if (!parse_double(tok[3], lon) || !parse_double(tok[4], lat) || !parse_double(tok[5], alt)) {
          return Error::ParseError;
        }
        float mx = 0.F;
        float my = 0.F;
        float mz = 0.F;
        lon_lat_alt_to_mercator(lon, lat, alt, mx, my, mz);
        e->position = {mx, my, mz};
      } else if (ieq(tok[2], "mercator") || ieq(tok[2], "m") || ieq(tok[2], "meters")) {
        if (tok.size() < 6) {
          return Error::ParseError;
        }
        double px = 0;
        double py = 0;
        double pz = 0;
        if (!parse_double(tok[3], px) || !parse_double(tok[4], py) || !parse_double(tok[5], pz)) {
          return Error::ParseError;
        }
        e->position = {static_cast<float>(px), static_cast<float>(py), static_cast<float>(pz)};
      } else {
        if (tok.size() < 5) {
          return Error::ParseError;
        }
        double px = 0;
        double py = 0;
        double pz = 0;
        if (!parse_double(tok[2], px) || !parse_double(tok[3], py) || !parse_double(tok[4], pz)) {
          return Error::ParseError;
        }
        e->position = {static_cast<float>(px), static_cast<float>(py), static_cast<float>(pz)};
      }
      continue;
    }
    if (ieq(cmd, "entity_vel")) {
      if (tok.size() < 5) {
        return Error::ParseError;
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return Error::ParseError;
      }
      double vx = 0;
      double vy = 0;
      double vz = 0;
      if (!parse_double(tok[2], vx) || !parse_double(tok[3], vy) || !parse_double(tok[4], vz)) {
        return Error::ParseError;
      }
      e->velocity = {static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz)};
      continue;
    }

    if (ieq(cmd, "entity_id")) {
      if (tok.size() < 3) {
        return Error::ParseError;
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return Error::ParseError;
      }
      e->external_id = tok[2];
      continue;
    }
    if (ieq(cmd, "faction")) {
      if (tok.size() < 3) {
        return Error::ParseError;
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return Error::ParseError;
      }
      e->faction = tok[2];
      continue;
    }
    if (ieq(cmd, "variant")) {
      if (tok.size() < 3) {
        return Error::ParseError;
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return Error::ParseError;
      }
      e->variant_ref = tok[2];
      continue;
    }
    if (ieq(cmd, "icon2d")) {
      if (tok.size() < 3) {
        return Error::ParseError;
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return Error::ParseError;
      }
      e->icon_2d_path = tok[2];
      continue;
    }
    if (ieq(cmd, "color")) {
      if (tok.size() < 3) {
        return Error::ParseError;
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return Error::ParseError;
      }
      if (tok.size() >= 5) {
        unsigned ru = 0;
        unsigned gu = 0;
        unsigned bu = 0;
        if (!parse_u8_255(tok[2], ru) || !parse_u8_255(tok[3], gu) || !parse_u8_255(tok[4], bu)) {
          return Error::ParseError;
        }
        e->has_display_color = true;
        e->display_color_r = static_cast<float>(ru) / 255.F;
        e->display_color_g = static_cast<float>(gu) / 255.F;
        e->display_color_b = static_cast<float>(bu) / 255.F;
      } else if (tok.size() == 3) {
        float rf = 0.F;
        float gf = 0.F;
        float bf = 0.F;
        if (!parse_color_token(tok[2], rf, gf, bf)) {
          return Error::ParseError;
        }
        e->has_display_color = true;
        e->display_color_r = rf;
        e->display_color_g = gf;
        e->display_color_b = bf;
      } else {
        return Error::ParseError;
      }
      continue;
    }
    if (ieq(cmd, "model3d")) {
      if (tok.size() < 3) {
        return Error::ParseError;
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return Error::ParseError;
      }
      e->model_3d_path = tok[2];
      continue;
    }
    if (ieq(cmd, "attr")) {
      if (tok.size() < 4) {
        return Error::ParseError;
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return Error::ParseError;
      }
      e->platform_attributes.push_back({tok[2], join_tokens(tok, 3)});
      continue;
    }
    if (ieq(cmd, "mparam")) {
      if (tok.size() < 5) {
        return Error::ParseError;
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return Error::ParseError;
      }
      cw::engine::ModelKind mk{};
      if (!cw::ok(model_from_token(tok[2], mk))) {
        return Error::ParseError;
      }
      ModelMountDesc* mount = nullptr;
      for (auto& m : e->mounts) {
        if (m.kind == mk) {
          mount = &m;
          break;
        }
      }
      if (!mount) {
        return Error::ParseError;
      }
      mount->params.push_back({tok[3], join_tokens(tok, 4)});
      continue;
    }
    if (ieq(cmd, "script_lua")) {
      if (tok.size() < 3) {
        return Error::ParseError;
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return Error::ParseError;
      }
      ScriptBindingDesc sb;
      sb.kind = ScriptBindingDesc::Kind::Lua;
      sb.resource_path = tok[2];
      if (tok.size() >= 5 && ieq(tok[3], "entry")) {
        sb.entry_symbol = tok[4];
      }
      e->script = sb;
      continue;
    }
    if (ieq(cmd, "script_blueprint")) {
      if (tok.size() < 3) {
        return Error::ParseError;
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return Error::ParseError;
      }
      ScriptBindingDesc sb;
      sb.kind = ScriptBindingDesc::Kind::Blueprint;
      sb.resource_path = tok[2];
      e->script = sb;
      continue;
    }

    if (ieq(cmd, "route")) {
      if (tok.size() < 3) {
        return Error::ParseError;
      }
      ScenarioRoute r;
      r.id = tok[1];
      r.display_name = tok[2];
      out.routes.push_back(std::move(r));
      continue;
    }
    if (ieq(cmd, "route_pt")) {
      if (tok.size() < 5) {
        return Error::ParseError;
      }
      ScenarioRoute* rt = nullptr;
      for (auto& r : out.routes) {
        if (r.id == tok[1]) {
          rt = &r;
          break;
        }
      }
      if (!rt) {
        return Error::ParseError;
      }
      double x = 0;
      double y = 0;
      double z = 0;
      if (!parse_double(tok[2], x) || !parse_double(tok[3], y) || !parse_double(tok[4], z)) {
        return Error::ParseError;
      }
      rt->waypoints.push_back(RouteWaypoint{static_cast<float>(x), static_cast<float>(y),
                                             static_cast<float>(z)});
      continue;
    }
    if (ieq(cmd, "route_pt_geo")) {
      if (tok.size() < 5) {
        return Error::ParseError;
      }
      ScenarioRoute* rt = nullptr;
      for (auto& r : out.routes) {
        if (r.id == tok[1]) {
          rt = &r;
          break;
        }
      }
      if (!rt) {
        return Error::ParseError;
      }
      double lon = 0;
      double lat = 0;
      double alt = 0;
      if (!parse_double(tok[2], lon) || !parse_double(tok[3], lat) || !parse_double(tok[4], alt)) {
        return Error::ParseError;
      }
      float mx = 0.F;
      float my = 0.F;
      float mz = 0.F;
      lon_lat_alt_to_mercator(lon, lat, alt, mx, my, mz);
      rt->waypoints.push_back(RouteWaypoint{mx, my, mz});
      continue;
    }

    if (ieq(cmd, "airspace_box")) {
      if (tok.size() < 8) {
        return Error::ParseError;
      }
      ScenarioAirspace a;
      a.id = tok[1];
      a.kind = AirspaceKind::Box;
      double v[6]{};
      for (int i = 0; i < 6; ++i) {
        if (!parse_double(tok[2 + static_cast<std::size_t>(i)], v[i])) {
          return Error::ParseError;
        }
      }
      a.box_min = {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])};
      a.box_max = {static_cast<float>(v[3]), static_cast<float>(v[4]), static_cast<float>(v[5])};
      out.airspaces.push_back(std::move(a));
      continue;
    }
    if (ieq(cmd, "airspace_box_geo")) {
      if (tok.size() < 8) {
        return Error::ParseError;
      }
      ScenarioAirspace a;
      a.id = tok[1];
      a.kind = AirspaceKind::Box;
      double min_lon = 0;
      double min_lat = 0;
      double min_alt = 0;
      double max_lon = 0;
      double max_lat = 0;
      double max_alt = 0;
      if (!parse_double(tok[2], min_lon) || !parse_double(tok[3], min_lat) ||
          !parse_double(tok[4], min_alt) || !parse_double(tok[5], max_lon) ||
          !parse_double(tok[6], max_lat) || !parse_double(tok[7], max_alt)) {
        return Error::ParseError;
      }
      float mx0 = 0.F;
      float my0 = 0.F;
      float mz0 = 0.F;
      float mx1 = 0.F;
      float my1 = 0.F;
      float mz1 = 0.F;
      lon_lat_alt_to_mercator(min_lon, min_lat, min_alt, mx0, my0, mz0);
      lon_lat_alt_to_mercator(max_lon, max_lat, max_alt, mx1, my1, mz1);
      a.box_min = {mx0, my0, mz0};
      a.box_max = {mx1, my1, mz1};
      out.airspaces.push_back(std::move(a));
      continue;
    }
    if (ieq(cmd, "airspace_poly")) {
      if (tok.size() < 2) {
        return Error::ParseError;
      }
      ScenarioAirspace a;
      a.id = tok[1];
      a.kind = AirspaceKind::Polygon;
      out.airspaces.push_back(std::move(a));
      continue;
    }
    if (ieq(cmd, "ap_vert")) {
      if (tok.size() < 5) {
        return Error::ParseError;
      }
      ScenarioAirspace* a = find_airspace(out, tok[1]);
      if (!a || a->kind != AirspaceKind::Polygon) {
        return Error::ParseError;
      }
      double x = 0;
      double y = 0;
      double z = 0;
      if (!parse_double(tok[2], x) || !parse_double(tok[3], y) || !parse_double(tok[4], z)) {
        return Error::ParseError;
      }
      a->polygon.push_back({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
      continue;
    }
    if (ieq(cmd, "ap_vert_geo")) {
      if (tok.size() < 5) {
        return Error::ParseError;
      }
      ScenarioAirspace* a = find_airspace(out, tok[1]);
      if (!a || a->kind != AirspaceKind::Polygon) {
        return Error::ParseError;
      }
      double lon = 0;
      double lat = 0;
      double alt = 0;
      if (!parse_double(tok[2], lon) || !parse_double(tok[3], lat) || !parse_double(tok[4], alt)) {
        return Error::ParseError;
      }
      float mx = 0.F;
      float my = 0.F;
      float mz = 0.F;
      lon_lat_alt_to_mercator(lon, lat, alt, mx, my, mz);
      a->polygon.push_back({mx, my, mz});
      continue;
    }
    if (ieq(cmd, "air_attr")) {
      if (tok.size() < 4) {
        return Error::ParseError;
      }
      ScenarioAirspace* a = find_airspace(out, tok[1]);
      if (!a) {
        return Error::ParseError;
      }
      a->attrs.push_back({tok[2], join_tokens(tok, 3)});
      continue;
    }

    if (ieq(cmd, "comm_node")) {
      if (tok.size() < 2) {
        return Error::ParseError;
      }
      CommNodeDesc n;
      n.id = tok[1];
      std::unordered_map<std::string, double> f;
      std::unordered_map<std::string, std::string> s;
      std::size_t i = 2;
      if (i + 1 < tok.size() && ieq(tok[i], "entity")) {
        n.bound_entity = tok[i + 1];
        i += 2;
      }
      const std::unordered_map<std::string, int> fk{{"bw", 0}, {"lat_ms", 0}};
      const std::unordered_map<std::string, int> sk;
      const Error pe = parse_kv_pairs(tok, i, fk, f, sk, s);
      if (!cw::ok(pe)) {
        return pe;
      }
      if (f.count("bw")) {
        n.bandwidth_bps = f["bw"];
      }
      if (f.count("lat_ms")) {
        n.latency_ms = f["lat_ms"];
      }
      out.comm_nodes.push_back(std::move(n));
      continue;
    }
    if (ieq(cmd, "comm_link")) {
      if (tok.size() < 3) {
        return Error::ParseError;
      }
      CommLinkDesc l;
      l.from_node = tok[1];
      l.to_node = tok[2];
      std::unordered_map<std::string, double> f;
      std::unordered_map<std::string, std::string> s;
      const std::unordered_map<std::string, int> fk{{"loss", 0}, {"delay_ms", 0}};
      const std::unordered_map<std::string, int> sk;
      const Error pe = parse_kv_pairs(tok, 3, fk, f, sk, s);
      if (!cw::ok(pe)) {
        return pe;
      }
      if (f.count("loss")) {
        l.packet_loss = f["loss"];
      }
      if (f.count("delay_ms")) {
        l.delay_ms = f["delay_ms"];
      }
      out.comm_links.push_back(std::move(l));
      continue;
    }

    return Error::ParseError;
  }

  if (version != 1 && version != 2) {
    return Error::ParseError;
  }
  if (out.entities.empty()) {
    return Error::ParseError;
  }
  out.version = version;
  return validate_scenario(out);
}

Error parse_scenario_file(const char* path_utf8, Scenario& out) {
  std::ifstream f(path_utf8, std::ios::binary);
  if (!f) {
    return Error::IOError;
  }
  std::ostringstream buf;
  buf << f.rdbuf();
  return parse_scenario_text(buf.str(), out);
}

}  // namespace cw::scenario
