#pragma once

#include <cctype>
#include <string_view>

namespace cw {

/// ASCII 大小写不敏感相等（`std::string` / `const char*` 可隐式转为 `string_view`）。
[[nodiscard]] inline bool ieq(std::string_view a, std::string_view b) noexcept {
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

/// 以 `\\0` 结尾的 C 字符串；任一为 `nullptr` 则仅当二者均为 `nullptr` 时相等。
[[nodiscard]] inline bool ieq_cstr(const char* a, const char* b) noexcept {
  if (a == nullptr || b == nullptr) {
    return a == b;
  }
  while (*a != '\0' && *b != '\0') {
    const unsigned char ua = static_cast<unsigned char>(*a);
    const unsigned char ub = static_cast<unsigned char>(*b);
    const char ca = (ua >= 'A' && ua <= 'Z') ? static_cast<char>(ua + 32) : *a;
    const char cb = (ub >= 'A' && ub <= 'Z') ? static_cast<char>(ub + 32) : *b;
    if (ca != cb) {
      return false;
    }
    ++a;
    ++b;
  }
  return *a == *b;
}

}  // namespace cw
