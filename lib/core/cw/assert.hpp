#pragma once

#include "cw/log.hpp"

#include <cstdlib>

/// Hard assert: logs and aborts. Prefer for invariant violations in engine code paths.
#define CW_ASSERT(cond)                                                                           \
  do {                                                                                            \
    if (!(cond)) {                                                                                \
      ::cw::log(::cw::LogLevel::Error, "ASSERT failed: " #cond " (" __FILE__ ":" CW_STRINGIZE(__LINE__) ")"); \
      std::abort();                                                                               \
    }                                                                                             \
  } while (0)

#define CW_STRINGIZE_IMPL(x) #x
#define CW_STRINGIZE(x) CW_STRINGIZE_IMPL(x)
