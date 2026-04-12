#include "cw/situation_view/asset_paths.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace cw::situation_view {

namespace {

std::string g_scenario_file_dir_utf8{};

std::string dirname_path_utf8(const std::string& path) {
  if (path.empty()) {
    return {};
  }
  const std::size_t p = path.find_last_of("\\/");
  if (p == std::string::npos) {
    return {};
  }
  return path.substr(0, p);
}

#ifdef _WIN32
FILE* fopen_utf8_rb_resolve(const char* path_utf8) noexcept {
  const int nw = MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, nullptr, 0);
  if (nw <= 0) {
    return std::fopen(path_utf8, "rb");
  }
  std::vector<wchar_t> w(static_cast<std::size_t>(nw));
  MultiByteToWideChar(CP_UTF8, 0, path_utf8, -1, w.data(), nw);
  return _wfopen(w.data(), L"rb");
}

bool path_readable_utf8(const std::string& p) noexcept {
  FILE* f = fopen_utf8_rb_resolve(p.c_str());
  if (f == nullptr) {
    return false;
  }
  std::fclose(f);
  return true;
}
#endif

}  // namespace

void set_scenario_directory_for_asset_search_utf8(const std::string& scenario_file_path_utf8) {
  g_scenario_file_dir_utf8 = scenario_file_parent_absolute_utf8(scenario_file_path_utf8);
}

std::string scenario_file_parent_absolute_utf8(const std::string& scen_path_utf8) {
  if (scen_path_utf8.empty()) {
    return {};
  }
  std::error_code ec;
  std::filesystem::path abs = std::filesystem::absolute(scen_path_utf8, ec);
  if (ec) {
    return dirname_path_utf8(scen_path_utf8);
  }
  abs = abs.lexically_normal();
  return abs.parent_path().generic_string();
}

void append_relative_asset_candidates(const std::string& rel_utf8, std::vector<std::string>& out) {
  if (!g_scenario_file_dir_utf8.empty()) {
    std::filesystem::path scen_dir(g_scenario_file_dir_utf8);
    std::filesystem::path root = (scen_dir / "..").lexically_normal();
    std::filesystem::path full = (root / rel_utf8).lexically_normal();
    out.push_back(full.generic_string());
  }
  out.push_back(rel_utf8);
  out.push_back(std::string("../") + rel_utf8);
  out.push_back(std::string("../../") + rel_utf8);
  out.push_back(std::string("../../../") + rel_utf8);
  out.push_back(std::string("../../../../") + rel_utf8);
#ifdef _WIN32
  {
    wchar_t wbuf[MAX_PATH]{};
    if (GetModuleFileNameW(nullptr, wbuf, MAX_PATH) != 0U) {
      std::wstring wexe(wbuf);
      const auto cut = wexe.find_last_of(L"\\/");
      if (cut != std::wstring::npos) {
        std::wstring dir = wexe.substr(0, cut);
        for (int up = 0; up <= 20; ++up) {
          std::wstring wpath = dir;
          for (int i = 0; i < up; ++i) {
            const auto p = wpath.find_last_of(L"\\/");
            if (p == std::wstring::npos) {
              wpath.clear();
              break;
            }
            wpath = wpath.substr(0, p);
          }
          if (wpath.empty()) {
            continue;
          }
          const std::wstring wrel(rel_utf8.begin(), rel_utf8.end());
          const std::wstring wfull = wpath + L'/' + wrel;
          const int nc = WideCharToMultiByte(CP_UTF8, 0, wfull.c_str(), -1, nullptr, 0, nullptr, nullptr);
          if (nc > 0) {
            std::string utf8(static_cast<std::size_t>(nc - 1), '\0');
            WideCharToMultiByte(CP_UTF8, 0, wfull.c_str(), -1, utf8.data(), nc, nullptr, nullptr);
            out.push_back(std::move(utf8));
          }
        }
      }
    }
  }
#endif
}

std::string resolve_asset_path_utf8(const char* user_path_utf8) {
  std::vector<std::string> cands;
  append_relative_asset_candidates(std::string(user_path_utf8), cands);
  for (const auto& p : cands) {
#ifdef _WIN32
    if (path_readable_utf8(p)) {
      return p;
    }
#else
    std::ifstream f(p, std::ios::binary);
    if (f) {
      return p;
    }
#endif
  }
  return std::string(user_path_utf8);
}

}  // namespace cw::situation_view
