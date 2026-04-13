#include "cw/scenario/parse.hpp"

#include "cw/math/constants.hpp"
#include "cw/string_match.hpp"

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

  实体扩展（name 须已存在；后写覆盖先写，与 entity_color 相同）：
    entity_pos <name> geo <lon°> <lat°> <alt_m>
    entity_pos <name> mercator|m|meters <mx> <my> <mz>
    entity_pos <name> <mx> <my> <mz>              # 墨卡托米 + 海拔米
    entity_vel <name> <vx> <vy> <vz>              # 机体系速度 m/s（默认 0）；见 entity_att
    entity_att <name> <yaw_deg> <pitch_deg> <roll_deg>  # 度；未写则 0
    entity_id <name> <external_id>
    entity_faction <name> <faction_token>
    entity_variant <name> <variant_ref>
    entity_icon2d <name> <path>
    entity_color <name> <r> <g> <b>              # 0..255，态势显示实体色
    entity_color <name> <#RRGGBB|#RGB>           # 十六进制，如 #ff8800 或 #f80
    entity_color <name> <color_name>             # 英文名或中文名，如 orange、skyblue、橙
    entity_model3d <name> <path>
    entity_attr <name> <key> <value...>        # value 为剩余整行
    entity_mparam <name> <model> <key> <value...>
    entity_script <name> <kind> <path> [entry <sym>]   # kind: lua | blueprint | bp

  航线：
    route <route_id> <display_name_one_token>
    route_pt <route_id> <x> <y> <z>
    route_attr <route_id> color <r> <g> <b>     # 0..255，与 entity_color 一致
    route_attr <route_id> color <#hex|color_name>
    route_attr <route_id> width <px>             # 线宽像素，(0,64]

  空域：
    airspace_box <id> <minx> <miny> <minz> <maxx> <maxy> <maxz>
    airspace_poly <id>
    ap_vert <id> <x> <y> <z>
    air_attr <id> <key> <value...>

  通信：
    comm_node <id> [entity <bound_name>] [bw <bps>] [lat_ms <ms>]  # bw、lat_ms 须 >= 0
    comm_link <from_id> <to_id> [loss <0..1>] [delay_ms <ms>]     # 须在两端 id 的 comm_node 之后；loss 为 [0,1]，delay_ms >= 0
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

Error model_from_token(const std::string& t, cw::ModelKind& out) {
  using cw::ModelKind;
  if (cw::ieq(t, "mover")) {
    out = ModelKind::Mover;
  } else if (cw::ieq(t, "sensor")) {
    out = ModelKind::Sensor;
  } else if (cw::ieq(t, "comdevice")) {
    out = ModelKind::Comdevice;
  } else if (cw::ieq(t, "processor")) {
    out = ModelKind::Processor;
  } else if (cw::ieq(t, "weapon")) {
    out = ModelKind::Weapon;
  } else if (cw::ieq(t, "signature")) {
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

ScenarioRoute* find_route(Scenario& sc, const std::string& id) {
  for (auto& r : sc.routes) {
    if (r.id == id) {
      return &r;
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
  constexpr double kMaxLat = 85.0511287798066;
  const double lat_clamped = std::max(-kMaxLat, std::min(kMaxLat, lat_deg));
  const double lon_rad = lon_deg * (cw::math::kPi / 180.0);
  const double lat_rad = lat_clamped * (cw::math::kPi / 180.0);
  double x = kR * lon_rad;
  double y = kR * std::log(std::tan(cw::math::kPi / 4.0 + lat_rad * 0.5));
  constexpr double kYMax = 20037508.34;
  y = std::max(-kYMax, std::min(kYMax, y));
  out_x = static_cast<float>(x);
  out_y = static_cast<float>(y);
  out_z = static_cast<float>(alt_m);
}

Error validate_fail(ParseDiagnostics* diag, ParseSubcode code) noexcept {
  if (diag != nullptr) {
    diag->line = 0;
    diag->subcode = code;
  }
  return Error::ParseError;
}

Error fail_line(ParseDiagnostics* diag, int line, ParseSubcode code) noexcept {
  if (diag != nullptr) {
    diag->line = line;
    diag->subcode = code;
  }
  return Error::ParseError;
}

Error validate_scenario(const Scenario& sc, ParseDiagnostics* diag) {
  std::unordered_set<std::string> seen_routes;
  for (const auto& r : sc.routes) {
    if (r.id.empty()) {
      return validate_fail(diag, ParseSubcode::EmptyRouteId);
    }
    if (seen_routes.count(r.id)) {
      return validate_fail(diag, ParseSubcode::DuplicateRouteId);
    }
    seen_routes.insert(r.id);
  }
  std::unordered_set<std::string> seen_air;
  for (const auto& a : sc.airspaces) {
    if (a.id.empty()) {
      return validate_fail(diag, ParseSubcode::EmptyAirspaceId);
    }
    if (seen_air.count(a.id)) {
      return validate_fail(diag, ParseSubcode::DuplicateAirspaceId);
    }
    seen_air.insert(a.id);
  }
  std::unordered_set<std::string> nodes;
  for (const auto& n : sc.comm_nodes) {
    if (n.id.empty()) {
      return validate_fail(diag, ParseSubcode::EmptyCommNodeId);
    }
    if (nodes.count(n.id)) {
      return validate_fail(diag, ParseSubcode::DuplicateCommNodeId);
    }
    nodes.insert(n.id);
  }
  for (const auto& l : sc.comm_links) {
    if (!nodes.count(l.from_node) || !nodes.count(l.to_node)) {
      return validate_fail(diag, ParseSubcode::CommLinkUnknownEndpoint);
    }
  }
  for (const auto& a : sc.airspaces) {
    if (a.kind == AirspaceKind::Polygon && a.polygon.size() < 3) {
      return validate_fail(diag, ParseSubcode::PolygonTooFewVertices);
    }
  }
  std::unordered_set<std::string> ent_names;
  for (const auto& e : sc.entities) {
    ent_names.insert(e.name);
  }
  for (const auto& n : sc.comm_nodes) {
    if (!n.bound_entity.empty() && !ent_names.count(n.bound_entity)) {
      return validate_fail(diag, ParseSubcode::CommNodeUnknownBoundEntity);
    }
  }
  return Error::Ok;
}

}  // namespace

Error parse_scenario_text(std::string_view text, Scenario& out, ParseDiagnostics* diag) {
  if (diag != nullptr) {
    diag->line = 0;
    diag->subcode = ParseSubcode::None;
  }
  out = Scenario{};
  int version = 0;
  std::unordered_set<std::string> route_ids_seen;
  std::unordered_set<std::string> airspace_ids_seen;
  std::unordered_set<std::string> comm_node_ids_seen;

  std::string chunk(text);
  std::istringstream in(chunk);
  std::string line;
  int line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
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

    if (cw::ieq(cmd, "version")) {
      if (tok.size() < 2) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      version = std::atoi(tok[1].c_str());
      if (version != 1 && version != 2) {
        return fail_line(diag, line_no, ParseSubcode::UnsupportedVersion);
      }
      continue;
    }

    if (cw::ieq(cmd, "entity")) {
      if (tok.size() < 3) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioEntityDesc ent;
      ent.name = tok[1];
      if (ent.name.empty()) {
        return fail_line(diag, line_no, ParseSubcode::EmptyEntityName);
      }
      if (find_entity(out, ent.name) != nullptr) {
        return fail_line(diag, line_no, ParseSubcode::DuplicateEntityName);
      }
      /// position 默认 0；velocity 为机体系，由 entity_att 转世界系；由 entity_pos、entity_vel、entity_att 设置。
      for (std::size_t i = 2; i < tok.size(); ++i) {
        ModelMountDesc md;
        const Error me = model_from_token(tok[i], md.kind);
        if (!cw::ok(me)) {
          return fail_line(diag, line_no, ParseSubcode::UnknownModelKind);
        }
        ent.mounts.push_back(std::move(md));
      }
      if (ent.mounts.empty()) {
        return fail_line(diag, line_no, ParseSubcode::EntityMissingModels);
      }
      out.entities.push_back(std::move(ent));
      continue;
    }

    if (cw::ieq(cmd, "entity_pos")) {
      if (tok.size() < 4) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return fail_line(diag, line_no, ParseSubcode::UnknownEntityName);
      }
      if (cw::ieq(tok[2], "geo") || cw::ieq(tok[2], "wgs84") || cw::ieq(tok[2], "ll")) {
        if (tok.size() < 6) {
          return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
        }
        double lon = 0;
        double lat = 0;
        double alt = 0;
        if (!parse_double(tok[3], lon) || !parse_double(tok[4], lat) || !parse_double(tok[5], alt)) {
          return fail_line(diag, line_no, ParseSubcode::ExpectedNumber);
        }
        float mx = 0.F;
        float my = 0.F;
        float mz = 0.F;
        lon_lat_alt_to_mercator(lon, lat, alt, mx, my, mz);
        e->position = {mx, my, mz};
      } else if (cw::ieq(tok[2], "mercator") || cw::ieq(tok[2], "m") || cw::ieq(tok[2], "meters")) {
        if (tok.size() < 6) {
          return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
        }
        double px = 0;
        double py = 0;
        double pz = 0;
        if (!parse_double(tok[3], px) || !parse_double(tok[4], py) || !parse_double(tok[5], pz)) {
          return fail_line(diag, line_no, ParseSubcode::ExpectedNumber);
        }
        e->position = {static_cast<float>(px), static_cast<float>(py), static_cast<float>(pz)};
      } else {
        if (tok.size() < 5) {
          return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
        }
        double px = 0;
        double py = 0;
        double pz = 0;
        if (!parse_double(tok[2], px) || !parse_double(tok[3], py) || !parse_double(tok[4], pz)) {
          return fail_line(diag, line_no, ParseSubcode::ExpectedNumber);
        }
        e->position = {static_cast<float>(px), static_cast<float>(py), static_cast<float>(pz)};
      }
      continue;
    }
    if (cw::ieq(cmd, "entity_vel")) {
      if (tok.size() < 5) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return fail_line(diag, line_no, ParseSubcode::UnknownEntityName);
      }
      double vx = 0;
      double vy = 0;
      double vz = 0;
      if (!parse_double(tok[2], vx) || !parse_double(tok[3], vy) || !parse_double(tok[4], vz)) {
        return fail_line(diag, line_no, ParseSubcode::ExpectedNumber);
      }
      e->velocity = {static_cast<float>(vx), static_cast<float>(vy), static_cast<float>(vz)};
      continue;
    }
    if (cw::ieq(cmd, "entity_att")) {
      if (tok.size() < 5) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioEntityDesc* ent = find_entity(out, tok[1]);
      if (!ent) {
        return fail_line(diag, line_no, ParseSubcode::UnknownEntityName);
      }
      double y = 0;
      double p = 0;
      double r = 0;
      if (!parse_double(tok[2], y) || !parse_double(tok[3], p) || !parse_double(tok[4], r)) {
        return fail_line(diag, line_no, ParseSubcode::ExpectedNumber);
      }
      ent->yaw_deg = static_cast<float>(y);
      ent->pitch_deg = static_cast<float>(p);
      ent->roll_deg = static_cast<float>(r);
      continue;
    }

    if (cw::ieq(cmd, "entity_id")) {
      if (tok.size() < 3) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return fail_line(diag, line_no, ParseSubcode::UnknownEntityName);
      }
      e->external_id = tok[2];
      continue;
    }
    if (cw::ieq(cmd, "entity_faction")) {
      if (tok.size() < 3) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return fail_line(diag, line_no, ParseSubcode::UnknownEntityName);
      }
      e->faction = tok[2];
      continue;
    }
    if (cw::ieq(cmd, "entity_variant")) {
      if (tok.size() < 3) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return fail_line(diag, line_no, ParseSubcode::UnknownEntityName);
      }
      e->variant_ref = tok[2];
      continue;
    }
    if (cw::ieq(cmd, "entity_icon2d")) {
      if (tok.size() < 3) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return fail_line(diag, line_no, ParseSubcode::UnknownEntityName);
      }
      e->icon_2d_path = tok[2];
      continue;
    }
    if (cw::ieq(cmd, "entity_color")) {
      if (tok.size() < 3) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return fail_line(diag, line_no, ParseSubcode::UnknownEntityName);
      }
      if (tok.size() >= 5) {
        unsigned ru = 0;
        unsigned gu = 0;
        unsigned bu = 0;
        if (!parse_u8_255(tok[2], ru) || !parse_u8_255(tok[3], gu) || !parse_u8_255(tok[4], bu)) {
          return fail_line(diag, line_no, ParseSubcode::InvalidColor);
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
          return fail_line(diag, line_no, ParseSubcode::InvalidColor);
        }
        e->has_display_color = true;
        e->display_color_r = rf;
        e->display_color_g = gf;
        e->display_color_b = bf;
      } else {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      continue;
    }
    if (cw::ieq(cmd, "entity_model3d")) {
      if (tok.size() < 3) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return fail_line(diag, line_no, ParseSubcode::UnknownEntityName);
      }
      e->model_3d_path = tok[2];
      continue;
    }
    if (cw::ieq(cmd, "entity_attr")) {
      if (tok.size() < 4) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return fail_line(diag, line_no, ParseSubcode::UnknownEntityName);
      }
      e->platform_attributes.push_back({tok[2], join_tokens(tok, 3)});
      continue;
    }
    if (cw::ieq(cmd, "entity_mparam")) {
      if (tok.size() < 5) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return fail_line(diag, line_no, ParseSubcode::UnknownEntityName);
      }
      cw::ModelKind mk{};
      if (!cw::ok(model_from_token(tok[2], mk))) {
        return fail_line(diag, line_no, ParseSubcode::UnknownModelKind);
      }
      ModelMountDesc* mount = nullptr;
      for (auto& m : e->mounts) {
        if (m.kind == mk) {
          mount = &m;
          break;
        }
      }
      if (!mount) {
        return fail_line(diag, line_no, ParseSubcode::MountKindNotFound);
      }
      mount->params.push_back({tok[3], join_tokens(tok, 4)});
      continue;
    }
    if (cw::ieq(cmd, "entity_script")) {
      if (tok.size() < 4) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioEntityDesc* e = find_entity(out, tok[1]);
      if (!e) {
        return fail_line(diag, line_no, ParseSubcode::UnknownEntityName);
      }
      const std::string& kind_tok = tok[2];
      ScriptBindingDesc sb;
      if (cw::ieq(kind_tok, "lua") || cw::ieq(kind_tok, "lua_script")) {
        sb.kind = ScriptBindingDesc::Kind::Lua;
      } else if (cw::ieq(kind_tok, "blueprint") || cw::ieq(kind_tok, "bp")) {
        sb.kind = ScriptBindingDesc::Kind::Blueprint;
      } else {
        return fail_line(diag, line_no, ParseSubcode::UnknownScriptBindingKind);
      }
      sb.resource_path = tok[3];
      if (sb.kind == ScriptBindingDesc::Kind::Lua) {
        if (tok.size() >= 6 && cw::ieq(tok[4], "entry")) {
          sb.entry_symbol = tok[5];
        }
      }
      e->script = std::move(sb);
      continue;
    }

    if (cw::ieq(cmd, "route")) {
      if (tok.size() < 3) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioRoute r;
      r.id = tok[1];
      r.display_name = tok[2];
      if (r.id.empty()) {
        return fail_line(diag, line_no, ParseSubcode::EmptyRouteId);
      }
      if (route_ids_seen.count(r.id)) {
        return fail_line(diag, line_no, ParseSubcode::DuplicateRouteId);
      }
      route_ids_seen.insert(r.id);
      out.routes.push_back(std::move(r));
      continue;
    }
    if (cw::ieq(cmd, "route_pt")) {
      if (tok.size() < 5) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioRoute* rt = nullptr;
      for (auto& r : out.routes) {
        if (r.id == tok[1]) {
          rt = &r;
          break;
        }
      }
      if (!rt) {
        return fail_line(diag, line_no, ParseSubcode::UnknownRouteId);
      }
      double x = 0;
      double y = 0;
      double z = 0;
      if (!parse_double(tok[2], x) || !parse_double(tok[3], y) || !parse_double(tok[4], z)) {
        return fail_line(diag, line_no, ParseSubcode::ExpectedNumber);
      }
      rt->waypoints.push_back(RouteWaypoint{static_cast<float>(x), static_cast<float>(y),
                                             static_cast<float>(z)});
      continue;
    }
    if (cw::ieq(cmd, "route_pt_geo")) {
      if (tok.size() < 5) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioRoute* rt = nullptr;
      for (auto& r : out.routes) {
        if (r.id == tok[1]) {
          rt = &r;
          break;
        }
      }
      if (!rt) {
        return fail_line(diag, line_no, ParseSubcode::UnknownRouteId);
      }
      double lon = 0;
      double lat = 0;
      double alt = 0;
      if (!parse_double(tok[2], lon) || !parse_double(tok[3], lat) || !parse_double(tok[4], alt)) {
        return fail_line(diag, line_no, ParseSubcode::ExpectedNumber);
      }
      float mx = 0.F;
      float my = 0.F;
      float mz = 0.F;
      lon_lat_alt_to_mercator(lon, lat, alt, mx, my, mz);
      rt->waypoints.push_back(RouteWaypoint{mx, my, mz});
      continue;
    }
    if (cw::ieq(cmd, "route_attr")) {
      if (tok.size() < 4) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioRoute* rt = find_route(out, tok[1]);
      if (!rt) {
        return fail_line(diag, line_no, ParseSubcode::UnknownRouteId);
      }
      if (cw::ieq(tok[2], "color")) {
        if (tok.size() >= 6) {
          unsigned ru = 0;
          unsigned gu = 0;
          unsigned bu = 0;
          if (!parse_u8_255(tok[3], ru) || !parse_u8_255(tok[4], gu) || !parse_u8_255(tok[5], bu)) {
            return fail_line(diag, line_no, ParseSubcode::InvalidColor);
          }
          rt->has_line_color = true;
          rt->line_r = static_cast<float>(ru) / 255.F;
          rt->line_g = static_cast<float>(gu) / 255.F;
          rt->line_b = static_cast<float>(bu) / 255.F;
        } else if (tok.size() == 4) {
          float rf = 0.F;
          float gf = 0.F;
          float bf = 0.F;
          if (!parse_color_token(tok[3], rf, gf, bf)) {
            return fail_line(diag, line_no, ParseSubcode::InvalidColor);
          }
          rt->has_line_color = true;
          rt->line_r = rf;
          rt->line_g = gf;
          rt->line_b = bf;
        } else {
          return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
        }
      } else if (cw::ieq(tok[2], "width")) {
        if (tok.size() < 4) {
          return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
        }
        double w = 0;
        if (!parse_double(tok[3], w) || !std::isfinite(w) || w <= 0.0 || w > 64.0) {
          return fail_line(diag, line_no, ParseSubcode::CommNumericOutOfRange);
        }
        rt->has_line_width = true;
        rt->line_width_px = static_cast<float>(w);
      } else {
        return fail_line(diag, line_no, ParseSubcode::RouteAttrUnknownKey);
      }
      continue;
    }

    if (cw::ieq(cmd, "airspace_box")) {
      if (tok.size() < 8) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioAirspace a;
      a.id = tok[1];
      a.kind = AirspaceKind::Box;
      double v[6]{};
      for (int i = 0; i < 6; ++i) {
        if (!parse_double(tok[2 + static_cast<std::size_t>(i)], v[i])) {
          return fail_line(diag, line_no, ParseSubcode::ExpectedNumber);
        }
      }
      a.box_min = {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])};
      a.box_max = {static_cast<float>(v[3]), static_cast<float>(v[4]), static_cast<float>(v[5])};
      if (a.id.empty()) {
        return fail_line(diag, line_no, ParseSubcode::EmptyAirspaceId);
      }
      if (airspace_ids_seen.count(a.id)) {
        return fail_line(diag, line_no, ParseSubcode::DuplicateAirspaceId);
      }
      airspace_ids_seen.insert(a.id);
      out.airspaces.push_back(std::move(a));
      continue;
    }
    if (cw::ieq(cmd, "airspace_box_geo")) {
      if (tok.size() < 8) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
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
        return fail_line(diag, line_no, ParseSubcode::ExpectedNumber);
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
      if (a.id.empty()) {
        return fail_line(diag, line_no, ParseSubcode::EmptyAirspaceId);
      }
      if (airspace_ids_seen.count(a.id)) {
        return fail_line(diag, line_no, ParseSubcode::DuplicateAirspaceId);
      }
      airspace_ids_seen.insert(a.id);
      out.airspaces.push_back(std::move(a));
      continue;
    }
    if (cw::ieq(cmd, "airspace_poly")) {
      if (tok.size() < 2) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioAirspace a;
      a.id = tok[1];
      a.kind = AirspaceKind::Polygon;
      if (a.id.empty()) {
        return fail_line(diag, line_no, ParseSubcode::EmptyAirspaceId);
      }
      if (airspace_ids_seen.count(a.id)) {
        return fail_line(diag, line_no, ParseSubcode::DuplicateAirspaceId);
      }
      airspace_ids_seen.insert(a.id);
      out.airspaces.push_back(std::move(a));
      continue;
    }
    if (cw::ieq(cmd, "ap_vert")) {
      if (tok.size() < 5) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioAirspace* a = find_airspace(out, tok[1]);
      if (!a) {
        return fail_line(diag, line_no, ParseSubcode::UnknownAirspaceId);
      }
      if (a->kind != AirspaceKind::Polygon) {
        return fail_line(diag, line_no, ParseSubcode::WrongAirspaceKindForVertex);
      }
      double x = 0;
      double y = 0;
      double z = 0;
      if (!parse_double(tok[2], x) || !parse_double(tok[3], y) || !parse_double(tok[4], z)) {
        return fail_line(diag, line_no, ParseSubcode::ExpectedNumber);
      }
      a->polygon.push_back({static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)});
      continue;
    }
    if (cw::ieq(cmd, "ap_vert_geo")) {
      if (tok.size() < 5) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioAirspace* a = find_airspace(out, tok[1]);
      if (!a) {
        return fail_line(diag, line_no, ParseSubcode::UnknownAirspaceId);
      }
      if (a->kind != AirspaceKind::Polygon) {
        return fail_line(diag, line_no, ParseSubcode::WrongAirspaceKindForVertex);
      }
      double lon = 0;
      double lat = 0;
      double alt = 0;
      if (!parse_double(tok[2], lon) || !parse_double(tok[3], lat) || !parse_double(tok[4], alt)) {
        return fail_line(diag, line_no, ParseSubcode::ExpectedNumber);
      }
      float mx = 0.F;
      float my = 0.F;
      float mz = 0.F;
      lon_lat_alt_to_mercator(lon, lat, alt, mx, my, mz);
      a->polygon.push_back({mx, my, mz});
      continue;
    }
    if (cw::ieq(cmd, "air_attr")) {
      if (tok.size() < 4) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      ScenarioAirspace* a = find_airspace(out, tok[1]);
      if (!a) {
        return fail_line(diag, line_no, ParseSubcode::UnknownAirspaceId);
      }
      a->attrs.push_back({tok[2], join_tokens(tok, 3)});
      continue;
    }

    if (cw::ieq(cmd, "comm_node")) {
      if (tok.size() < 2) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      CommNodeDesc n;
      n.id = tok[1];
      std::unordered_map<std::string, double> f;
      std::unordered_map<std::string, std::string> s;
      std::size_t i = 2;
      if (i + 1 < tok.size() && cw::ieq(tok[i], "entity")) {
        n.bound_entity = tok[i + 1];
        i += 2;
      }
      const std::unordered_map<std::string, int> fk{{"bw", 0}, {"lat_ms", 0}};
      const std::unordered_map<std::string, int> sk;
      const Error pe = parse_kv_pairs(tok, i, fk, f, sk, s);
      if (!cw::ok(pe)) {
        return fail_line(diag, line_no, ParseSubcode::KeyValueSyntaxError);
      }
      if (f.count("bw")) {
        const double bw = f["bw"];
        if (!std::isfinite(bw) || bw < 0.0) {
          return fail_line(diag, line_no, ParseSubcode::CommNumericOutOfRange);
        }
        n.bandwidth_bps = bw;
      }
      if (f.count("lat_ms")) {
        const double lat = f["lat_ms"];
        if (!std::isfinite(lat) || lat < 0.0) {
          return fail_line(diag, line_no, ParseSubcode::CommNumericOutOfRange);
        }
        n.latency_ms = lat;
      }
      if (n.id.empty()) {
        return fail_line(diag, line_no, ParseSubcode::EmptyCommNodeId);
      }
      if (comm_node_ids_seen.count(n.id)) {
        return fail_line(diag, line_no, ParseSubcode::DuplicateCommNodeId);
      }
      if (!n.bound_entity.empty() && find_entity(out, n.bound_entity) == nullptr) {
        return fail_line(diag, line_no, ParseSubcode::UnknownEntityName);
      }
      comm_node_ids_seen.insert(n.id);
      out.comm_nodes.push_back(std::move(n));
      continue;
    }
    if (cw::ieq(cmd, "comm_link")) {
      if (tok.size() < 3) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      CommLinkDesc l;
      l.from_node = tok[1];
      l.to_node = tok[2];
      if (l.from_node.empty() || l.to_node.empty()) {
        return fail_line(diag, line_no, ParseSubcode::LineSyntaxError);
      }
      if (!comm_node_ids_seen.count(l.from_node) || !comm_node_ids_seen.count(l.to_node)) {
        return fail_line(diag, line_no, ParseSubcode::CommLinkUnknownEndpoint);
      }
      std::unordered_map<std::string, double> f;
      std::unordered_map<std::string, std::string> s;
      const std::unordered_map<std::string, int> fk{{"loss", 0}, {"delay_ms", 0}};
      const std::unordered_map<std::string, int> sk;
      const Error pe = parse_kv_pairs(tok, 3, fk, f, sk, s);
      if (!cw::ok(pe)) {
        return fail_line(diag, line_no, ParseSubcode::KeyValueSyntaxError);
      }
      if (f.count("loss")) {
        const double loss = f["loss"];
        if (!std::isfinite(loss) || loss < 0.0 || loss > 1.0) {
          return fail_line(diag, line_no, ParseSubcode::CommNumericOutOfRange);
        }
        l.packet_loss = loss;
      }
      if (f.count("delay_ms")) {
        const double dms = f["delay_ms"];
        if (!std::isfinite(dms) || dms < 0.0) {
          return fail_line(diag, line_no, ParseSubcode::CommNumericOutOfRange);
        }
        l.delay_ms = dms;
      }
      out.comm_links.push_back(std::move(l));
      continue;
    }

    return fail_line(diag, line_no, ParseSubcode::UnknownCommand);
  }

  if (version != 1 && version != 2) {
    return fail_line(diag, 0, version == 0 ? ParseSubcode::MissingVersion : ParseSubcode::UnsupportedVersion);
  }
  if (out.entities.empty()) {
    return fail_line(diag, 0, ParseSubcode::NoEntities);
  }
  out.version = version;
  const Error v = validate_scenario(out, diag);
  if (!cw::ok(v)) {
    return v;
  }
  if (diag != nullptr) {
    diag->line = 0;
    diag->subcode = ParseSubcode::None;
  }
  return Error::Ok;
}

Error parse_scenario_file(const char* path_utf8, Scenario& out, ParseDiagnostics* diag) {
  std::ifstream f(path_utf8, std::ios::binary);
  if (!f) {
    return Error::IOError;
  }
  std::ostringstream buf;
  buf << f.rdbuf();
  return parse_scenario_text(buf.str(), out, diag);
}

}  // namespace cw::scenario
