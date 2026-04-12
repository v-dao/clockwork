#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include "cw/render/globe_program.hpp"

#include "cw/mat4.hpp"
#include "cw/render/world_vector_merc.hpp"
#include "cw/log.hpp"

#include <GL/gl.h>

#ifndef GL_VERTEX_SHADER
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_TEXTURE0 0x84C0
#define GL_TEXTURE1 0x84C1
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_CLAMP_TO_EDGE 0x812F
#endif

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

namespace cw::render {
namespace {

using GlCreateShader = GLuint(APIENTRY*)(GLenum);
using GlShaderSource = void(APIENTRY*)(GLuint, GLsizei, const char* const*, const GLint*);
using GlCompileShader = void(APIENTRY*)(GLuint);
using GlGetShaderiv = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GlGetShaderInfoLog = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
using GlDeleteShader = void(APIENTRY*)(GLuint);
using GlCreateProgram = GLuint(APIENTRY*)();
using GlAttachShader = void(APIENTRY*)(GLuint, GLuint);
using GlLinkProgram = void(APIENTRY*)(GLuint);
using GlGetProgramiv = void(APIENTRY*)(GLuint, GLenum, GLint*);
using GlGetProgramInfoLog = void(APIENTRY*)(GLuint, GLsizei, GLsizei*, char*);
using GlDeleteProgram = void(APIENTRY*)(GLuint);
using GlUseProgram = void(APIENTRY*)(GLuint);
using GlGetUniformLocation = GLint(APIENTRY*)(GLuint, const char*);
using GlUniformMatrix4fv = void(APIENTRY*)(GLint, GLsizei, GLboolean, const GLfloat*);
using GlUniform1i = void(APIENTRY*)(GLint, GLint);
using GlUniform3f = void(APIENTRY*)(GLint, GLfloat, GLfloat, GLfloat);
using GlUniform1f = void(APIENTRY*)(GLint, GLfloat);
using GlBindAttribLocation = void(APIENTRY*)(GLuint, GLuint, const char*);
using GlGenVertexArrays = void(APIENTRY*)(GLsizei, GLuint*);
using GlBindVertexArray = void(APIENTRY*)(GLuint);
using GlDeleteVertexArrays = void(APIENTRY*)(GLsizei, const GLuint*);
using GlGenBuffers = void(APIENTRY*)(GLsizei, GLuint*);
using GlBindBuffer = void(APIENTRY*)(GLenum, GLuint);
using GlBufferData = void(APIENTRY*)(GLenum, std::ptrdiff_t, const void*, GLenum);
using GlDeleteBuffers = void(APIENTRY*)(GLsizei, const GLuint*);
using GlEnableVertexAttribArray = void(APIENTRY*)(GLuint);
using GlVertexAttribPointer = void(APIENTRY*)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void*);
using GlDrawElements = void(APIENTRY*)(GLenum, GLsizei, GLenum, const void*);
using GlActiveTexture = void(APIENTRY*)(GLenum);
using GlGenFramebuffers = void(APIENTRY*)(GLsizei, GLuint*);
using GlBindFramebuffer = void(APIENTRY*)(GLenum, GLuint);
using GlFramebufferTexture2D = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
using GlCheckFramebufferStatus = GLenum(APIENTRY*)(GLenum);
using GlDeleteFramebuffers = void(APIENTRY*)(GLsizei, const GLuint*);

GlCreateShader fn_glCreateShader = nullptr;
GlShaderSource fn_glShaderSource = nullptr;
GlCompileShader fn_glCompileShader = nullptr;
GlGetShaderiv fn_glGetShaderiv = nullptr;
GlGetShaderInfoLog fn_glGetShaderInfoLog = nullptr;
GlDeleteShader fn_glDeleteShader = nullptr;
GlCreateProgram fn_glCreateProgram = nullptr;
GlAttachShader fn_glAttachShader = nullptr;
GlLinkProgram fn_glLinkProgram = nullptr;
GlGetProgramiv fn_glGetProgramiv = nullptr;
GlGetProgramInfoLog fn_glGetProgramInfoLog = nullptr;
GlDeleteProgram fn_glDeleteProgram = nullptr;
GlUseProgram fn_glUseProgram = nullptr;
GlGetUniformLocation fn_glGetUniformLocation = nullptr;
GlUniformMatrix4fv fn_glUniformMatrix4fv = nullptr;
GlUniform1i fn_glUniform1i = nullptr;
GlUniform3f fn_glUniform3f = nullptr;
GlUniform1f fn_glUniform1f = nullptr;
GlBindAttribLocation fn_glBindAttribLocation = nullptr;
GlGenVertexArrays fn_glGenVertexArrays = nullptr;
GlBindVertexArray fn_glBindVertexArray = nullptr;
GlDeleteVertexArrays fn_glDeleteVertexArrays = nullptr;
GlGenBuffers fn_glGenBuffers = nullptr;
GlBindBuffer fn_glBindBuffer = nullptr;
GlBufferData fn_glBufferData = nullptr;
GlDeleteBuffers fn_glDeleteBuffers = nullptr;
GlEnableVertexAttribArray fn_glEnableVertexAttribArray = nullptr;
GlVertexAttribPointer fn_glVertexAttribPointer = nullptr;
GlDrawElements fn_glDrawElements = nullptr;
GlActiveTexture fn_glActiveTexture = nullptr;
GlGenFramebuffers fn_glGenFramebuffers = nullptr;
GlBindFramebuffer fn_glBindFramebuffer = nullptr;
GlFramebufferTexture2D fn_glFramebufferTexture2D = nullptr;
GlCheckFramebufferStatus fn_glCheckFramebufferStatus = nullptr;
GlDeleteFramebuffers fn_glDeleteFramebuffers = nullptr;

bool g_procs_loaded = false;
bool g_ready = false;
GLuint g_program = 0;
GLint g_u_mvp = -1;
GLint g_u_basemap = -1;
GLint g_u_ocean = -1;
GLint g_u_has_basemap = -1;
GLint g_u_light = -1;
GLint g_u_ambient = -1;
GLint g_u_merc_atlas = -1;
GLint g_u_use_merc_atlas = -1;
GLint g_u_lighting = -1;
GLuint g_vao = 0;
GLuint g_vbo = 0;
GLuint g_ibo = 0;
GLsizei g_index_count = 0;

GLuint g_merc_atlas_tex = 0;
GLuint g_merc_fbo = 0;
bool g_merc_atlas_valid = false;

template <typename T>
bool load_proc(T& out, const char* name) {
  /// WGL 返回 PROC，经 void* 转换避免 -Wcast-function-type。
  auto p = reinterpret_cast<T>(reinterpret_cast<void*>(wglGetProcAddress(name)));
  if (p == nullptr) {
    cw::log(cw::LogLevel::Error, std::string("globe_program: missing GL proc ") += name);
    return false;
  }
  out = p;
  return true;
}

bool load_all_procs() {
  return load_proc(fn_glCreateShader, "glCreateShader") && load_proc(fn_glShaderSource, "glShaderSource") &&
         load_proc(fn_glCompileShader, "glCompileShader") && load_proc(fn_glGetShaderiv, "glGetShaderiv") &&
         load_proc(fn_glGetShaderInfoLog, "glGetShaderInfoLog") &&
         load_proc(fn_glDeleteShader, "glDeleteShader") && load_proc(fn_glCreateProgram, "glCreateProgram") &&
         load_proc(fn_glAttachShader, "glAttachShader") && load_proc(fn_glLinkProgram, "glLinkProgram") &&
         load_proc(fn_glGetProgramiv, "glGetProgramiv") &&
         load_proc(fn_glGetProgramInfoLog, "glGetProgramInfoLog") &&
         load_proc(fn_glDeleteProgram, "glDeleteProgram") && load_proc(fn_glUseProgram, "glUseProgram") &&
         load_proc(fn_glGetUniformLocation, "glGetUniformLocation") &&
         load_proc(fn_glUniformMatrix4fv, "glUniformMatrix4fv") && load_proc(fn_glUniform1i, "glUniform1i") &&
         load_proc(fn_glUniform3f, "glUniform3f") && load_proc(fn_glUniform1f, "glUniform1f") &&
         load_proc(fn_glBindAttribLocation, "glBindAttribLocation") &&
         load_proc(fn_glGenVertexArrays, "glGenVertexArrays") &&
         load_proc(fn_glBindVertexArray, "glBindVertexArray") &&
         load_proc(fn_glDeleteVertexArrays, "glDeleteVertexArrays") &&
         load_proc(fn_glGenBuffers, "glGenBuffers") && load_proc(fn_glBindBuffer, "glBindBuffer") &&
         load_proc(fn_glBufferData, "glBufferData") && load_proc(fn_glDeleteBuffers, "glDeleteBuffers") &&
         load_proc(fn_glEnableVertexAttribArray, "glEnableVertexAttribArray") &&
         load_proc(fn_glVertexAttribPointer, "glVertexAttribPointer") &&
         load_proc(fn_glDrawElements, "glDrawElements") && load_proc(fn_glActiveTexture, "glActiveTexture") &&
         load_proc(fn_glGenFramebuffers, "glGenFramebuffers") &&
         load_proc(fn_glBindFramebuffer, "glBindFramebuffer") &&
         load_proc(fn_glFramebufferTexture2D, "glFramebufferTexture2D") &&
         load_proc(fn_glCheckFramebufferStatus, "glCheckFramebufferStatus") &&
         load_proc(fn_glDeleteFramebuffers, "glDeleteFramebuffers");
}

void merc_atlas_destroy() noexcept {
  if (g_merc_fbo != 0U && fn_glDeleteFramebuffers != nullptr) {
    fn_glDeleteFramebuffers(1, &g_merc_fbo);
    g_merc_fbo = 0;
  }
  if (g_merc_atlas_tex != 0U) {
    glDeleteTextures(1, &g_merc_atlas_tex);
    g_merc_atlas_tex = 0;
  }
  g_merc_atlas_valid = false;
}

bool check_shader(GLuint sh, const char* what) {
  GLint ok = 0;
  fn_glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
  if (ok != 0) {
    return true;
  }
  GLint len = 0;
  fn_glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
  std::vector<char> buf(static_cast<std::size_t>(std::max(1, len)));
  GLsizei got = 0;
  fn_glGetShaderInfoLog(sh, static_cast<GLsizei>(buf.size()), &got, buf.data());
  cw::log(cw::LogLevel::Error,
          std::string("globe_program: ") + what + " compile: " + std::string(buf.data(), buf.data() + got));
  return false;
}

bool check_program(GLuint pr) {
  GLint ok = 0;
  fn_glGetProgramiv(pr, GL_LINK_STATUS, &ok);
  if (ok != 0) {
    return true;
  }
  GLint len = 0;
  fn_glGetProgramiv(pr, GL_INFO_LOG_LENGTH, &len);
  std::vector<char> buf(static_cast<std::size_t>(std::max(1, len)));
  GLsizei got = 0;
  fn_glGetProgramInfoLog(pr, static_cast<GLsizei>(buf.size()), &got, buf.data());
  cw::log(cw::LogLevel::Error, std::string("globe_program: link: ") + std::string(buf.data(), buf.data() + got));
  return false;
}

GLuint compile_shader(GLenum type, const char* src) {
  const GLuint sh = fn_glCreateShader(type);
  if (sh == 0U) {
    return 0;
  }
  const char* p = src;
  fn_glShaderSource(sh, 1, &p, nullptr);
  fn_glCompileShader(sh);
  return sh;
}

void build_sphere_mesh(int stacks, int slices) {
  const float kPi = 3.14159265F;
  std::vector<GLfloat> verts;
  verts.reserve(static_cast<std::size_t>((stacks + 1) * (slices + 1) * 3U));
  for (int i = 0; i <= stacks; ++i) {
    const float tv = static_cast<float>(i) / static_cast<float>(stacks);
    const float phi = -kPi * 0.5F + tv * kPi;
    const float y = std::sin(phi);
    const float rr = std::cos(phi);
    for (int j = 0; j <= slices; ++j) {
      const float tu = static_cast<float>(j) / static_cast<float>(slices);
      const float th = tu * 2.F * kPi;
      const float x = rr * std::sin(th);
      const float z = rr * std::cos(th);
      verts.push_back(x);
      verts.push_back(y);
      verts.push_back(z);
    }
  }
  std::vector<GLuint> idx;
  idx.reserve(static_cast<std::size_t>(stacks * slices * 6U));
  for (int i = 0; i < stacks; ++i) {
    for (int j = 0; j < slices; ++j) {
      const int a = i * (slices + 1) + j;
      const int b = a + 1;
      const int c = a + (slices + 1);
      const int d = c + 1;
      idx.push_back(static_cast<GLuint>(a));
      idx.push_back(static_cast<GLuint>(c));
      idx.push_back(static_cast<GLuint>(b));
      idx.push_back(static_cast<GLuint>(b));
      idx.push_back(static_cast<GLuint>(c));
      idx.push_back(static_cast<GLuint>(d));
    }
  }
  g_index_count = static_cast<GLsizei>(idx.size());
  fn_glGenVertexArrays(1, &g_vao);
  fn_glGenBuffers(1, &g_vbo);
  fn_glGenBuffers(1, &g_ibo);
  fn_glBindVertexArray(g_vao);
  fn_glBindBuffer(GL_ARRAY_BUFFER, g_vbo);
  fn_glBufferData(GL_ARRAY_BUFFER,
                  static_cast<std::ptrdiff_t>(verts.size() * sizeof(GLfloat)), verts.data(), GL_STATIC_DRAW);
  fn_glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, g_ibo);
  fn_glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                  static_cast<std::ptrdiff_t>(idx.size() * sizeof(GLuint)), idx.data(), GL_STATIC_DRAW);
  fn_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, nullptr);
  fn_glEnableVertexAttribArray(0);
  fn_glBindVertexArray(0);
}

const char* kVert = R"GL(
#version 330 compatibility
uniform mat4 u_mvp;
layout(location = 0) in vec3 a_pos;
out vec3 v_world;
void main() {
  v_world = a_pos;
  gl_Position = u_mvp * vec4(a_pos, 1.0);
}
)GL";

const char* kFrag = R"GL(
#version 330 compatibility
in vec3 v_world;
uniform sampler2D u_basemap;
uniform sampler2D u_merc_atlas;
uniform vec3 u_ocean;
uniform int u_has_basemap;
uniform int u_use_merc_atlas;
uniform vec3 u_lightDir;
uniform float u_ambient;
uniform int u_lighting;
void main() {
  vec3 n = normalize(v_world);
  float lon = atan(n.x, n.z);
  float lat = asin(clamp(n.y, -1.0, 1.0));
  float pi = 3.14159265359;
  vec2 uv_eq = vec2(lon / (2.0 * pi) + 0.5, 0.5 - lat / pi);
  vec3 base = u_ocean;
  if (u_use_merc_atlas != 0) {
    float R = 6378137.0;
    float YM = 20037508.34;
    float latc = clamp(lat, -1.567, 1.567);
    float xm = lon * R;
    float ym = R * log(tan(pi * 0.25 + latc * 0.5));
    xm = clamp(xm, -YM, YM);
    ym = clamp(ym, -YM, YM);
    vec2 muv = vec2((xm + YM) / (2.0 * YM), (ym + YM) / (2.0 * YM));
    base = texture(u_merc_atlas, muv).rgb;
  } else if (u_has_basemap != 0) {
    base = texture(u_basemap, uv_eq).rgb;
  }
  float ndl = 1.0;
  if (u_lighting != 0) {
    ndl = u_ambient + (1.0 - u_ambient) * max(dot(n, normalize(u_lightDir)), 0.0);
  }
  gl_FragColor = vec4(base * ndl, 1.0);
}
)GL";

}  // namespace

bool globe_program_try_init() noexcept {
  if (g_ready) {
    return true;
  }
  if (!g_procs_loaded) {
    if (!load_all_procs()) {
      return false;
    }
    g_procs_loaded = true;
  }

  const GLuint vs = compile_shader(GL_VERTEX_SHADER, kVert);
  if (vs == 0U || !check_shader(vs, "vertex")) {
    if (vs != 0U) {
      fn_glDeleteShader(vs);
    }
    return false;
  }
  const GLuint fs = compile_shader(GL_FRAGMENT_SHADER, kFrag);
  if (fs == 0U || !check_shader(fs, "fragment")) {
    fn_glDeleteShader(vs);
    if (fs != 0U) {
      fn_glDeleteShader(fs);
    }
    return false;
  }
  g_program = fn_glCreateProgram();
  if (g_program == 0U) {
    fn_glDeleteShader(vs);
    fn_glDeleteShader(fs);
    cw::log(cw::LogLevel::Error, "globe_program: glCreateProgram failed");
    return false;
  }
  fn_glBindAttribLocation(g_program, 0, "a_pos");
  fn_glAttachShader(g_program, vs);
  fn_glAttachShader(g_program, fs);
  fn_glLinkProgram(g_program);
  fn_glDeleteShader(vs);
  fn_glDeleteShader(fs);
  if (!check_program(g_program)) {
    fn_glDeleteProgram(g_program);
    g_program = 0;
    return false;
  }

  g_u_mvp = fn_glGetUniformLocation(g_program, "u_mvp");
  g_u_basemap = fn_glGetUniformLocation(g_program, "u_basemap");
  g_u_ocean = fn_glGetUniformLocation(g_program, "u_ocean");
  g_u_has_basemap = fn_glGetUniformLocation(g_program, "u_has_basemap");
  g_u_light = fn_glGetUniformLocation(g_program, "u_lightDir");
  g_u_ambient = fn_glGetUniformLocation(g_program, "u_ambient");
  g_u_merc_atlas = fn_glGetUniformLocation(g_program, "u_merc_atlas");
  g_u_use_merc_atlas = fn_glGetUniformLocation(g_program, "u_use_merc_atlas");
  g_u_lighting = fn_glGetUniformLocation(g_program, "u_lighting");
  if (g_u_mvp < 0 || g_u_basemap < 0 || g_u_ocean < 0 || g_u_has_basemap < 0 || g_u_light < 0 || g_u_ambient < 0 ||
      g_u_merc_atlas < 0 || g_u_use_merc_atlas < 0 || g_u_lighting < 0) {
    cw::log(cw::LogLevel::Error, "globe_program: uniform lookup failed");
    fn_glDeleteProgram(g_program);
    g_program = 0;
    return false;
  }

  build_sphere_mesh(96, 192);
  if (g_vao == 0U || g_index_count == 0) {
    cw::log(cw::LogLevel::Error, "globe_program: mesh failed");
    globe_program_shutdown();
    return false;
  }

  g_ready = true;
  return true;
}

void globe_program_shutdown() noexcept {
  merc_atlas_destroy();
  if (g_vao != 0U && fn_glDeleteVertexArrays != nullptr) {
    fn_glDeleteVertexArrays(1, &g_vao);
    g_vao = 0;
  }
  if (g_vbo != 0U && fn_glDeleteBuffers != nullptr) {
    fn_glDeleteBuffers(1, &g_vbo);
    g_vbo = 0;
  }
  if (g_ibo != 0U && fn_glDeleteBuffers != nullptr) {
    fn_glDeleteBuffers(1, &g_ibo);
    g_ibo = 0;
  }
  g_index_count = 0;
  if (g_program != 0U && fn_glDeleteProgram != nullptr) {
    fn_glDeleteProgram(g_program);
    g_program = 0;
  }
  g_ready = false;
}

bool globe_program_ready() noexcept { return g_ready; }

bool globe_merc_atlas_build_from_vector_land(const WorldVectorMerc& wv) noexcept {
  merc_atlas_destroy();
  if (!g_ready || !wv.valid() || fn_glGenFramebuffers == nullptr) {
    return false;
  }
  constexpr int kW = 4096;
  constexpr int kH = 2048;
  glGenTextures(1, &g_merc_atlas_tex);
  if (g_merc_atlas_tex == 0U) {
    return false;
  }
  glBindTexture(GL_TEXTURE_2D, g_merc_atlas_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, kW, kH, 0, GL_RGB, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  /// 放大用最近邻采样，减轻岸线处双线性羽化，与矢量海岸线对齐更好（略呈阶梯可接受）。
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);

  fn_glGenFramebuffers(1, &g_merc_fbo);
  if (g_merc_fbo == 0U) {
    glDeleteTextures(1, &g_merc_atlas_tex);
    g_merc_atlas_tex = 0;
    return false;
  }
  fn_glBindFramebuffer(GL_FRAMEBUFFER, g_merc_fbo);
  fn_glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_merc_atlas_tex, 0);
  if (fn_glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    cw::log(cw::LogLevel::Error, "globe_program: merc atlas FBO incomplete");
    merc_atlas_destroy();
    return false;
  }

  GLint viewport[4]{};
  glGetIntegerv(GL_VIEWPORT, viewport);
  fn_glUseProgram(0);
  glViewport(0, 0, kW, kH);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_CULL_FACE);
  glDisable(GL_TEXTURE_2D);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  constexpr double kYm = 20037508.34;
  glOrtho(-kYm, kYm, -kYm, kYm, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();
  glClearColor(0.078, 0.227, 0.463, 1.0);
  glClear(GL_COLOR_BUFFER_BIT);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  /// 不透明陆块：避免与洋面 alpha 混合产生灰边，与海岸线矢量对比时更贴边。
  glColor4f(0.2F, 0.62F, 0.28F, 1.0F);
  wv.draw_land_fill();
  glDisable(GL_BLEND);
  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();
  glMatrixMode(GL_MODELVIEW);
  fn_glBindFramebuffer(GL_FRAMEBUFFER, 0);
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

  g_merc_atlas_valid = true;
  return true;
}

bool globe_merc_atlas_valid() noexcept { return g_merc_atlas_valid && g_merc_atlas_tex != 0U; }

void draw_globe_sphere_glsl(GLuint tex_gl, const float ocean_rgb[3], bool enable_lighting,
                            bool use_land_basemap) noexcept {
  if (!g_ready || g_program == 0U) {
    return;
  }
  GLdouble proj[16]{};
  GLdouble mod[16]{};
  glGetDoublev(GL_PROJECTION_MATRIX, proj);
  glGetDoublev(GL_MODELVIEW_MATRIX, mod);
  GLdouble clip[16]{};
  cw::math::mat4_mul_col_major(proj, mod, clip);
  GLfloat mvp[16];
  for (int i = 0; i < 16; ++i) {
    mvp[i] = static_cast<GLfloat>(clip[i]);
  }

  fn_glUseProgram(g_program);
  fn_glUniformMatrix4fv(g_u_mvp, 1, GL_FALSE, mvp);
  fn_glUniform3f(g_u_ocean, ocean_rgb[0], ocean_rgb[1], ocean_rgb[2]);
  const int use_merc =
      (use_land_basemap && globe_merc_atlas_valid()) ? 1 : 0;
  const int has =
      (use_land_basemap && use_merc == 0 && tex_gl != 0U) ? 1 : 0;
  fn_glUniform1i(g_u_use_merc_atlas, use_merc);
  fn_glUniform1i(g_u_has_basemap, has);
  fn_glUniform1i(g_u_basemap, 0);
  fn_glUniform1i(g_u_merc_atlas, 1);
  fn_glUniform3f(g_u_light, 0.35F, 0.72F, 0.45F);
  fn_glUniform1f(g_u_ambient, 0.42F);
  fn_glUniform1i(g_u_lighting, enable_lighting ? 1 : 0);
  fn_glActiveTexture(GL_TEXTURE1);
  if (use_merc != 0) {
    glBindTexture(GL_TEXTURE_2D, g_merc_atlas_tex);
  } else {
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  fn_glActiveTexture(GL_TEXTURE0);
  if (has != 0) {
    glBindTexture(GL_TEXTURE_2D, tex_gl);
  } else {
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  fn_glBindVertexArray(g_vao);
  fn_glDrawElements(GL_TRIANGLES, g_index_count, GL_UNSIGNED_INT, nullptr);
  fn_glBindVertexArray(0);
  fn_glUseProgram(0);
  glBindTexture(GL_TEXTURE_2D, 0);
  fn_glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, 0);
  fn_glActiveTexture(GL_TEXTURE0);
}

}  // namespace cw::render

#else  // !_WIN32

#include "cw/render/globe_program.hpp"

namespace cw::render {

bool globe_program_try_init() noexcept { return false; }
void globe_program_shutdown() noexcept {}
bool globe_program_ready() noexcept { return false; }
bool globe_merc_atlas_build_from_vector_land(const WorldVectorMerc&) noexcept { return false; }
bool globe_merc_atlas_valid() noexcept { return false; }
void draw_globe_sphere_glsl(GLuint, const float*, bool, bool) noexcept {}

}  // namespace cw::render

#endif  // _WIN32
