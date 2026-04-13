#pragma once

#include <cstdint>
#include <istream>

namespace cw::render {

/// Read one little-endian `uint32` from a binary stream; returns false on EOF / short read.
[[nodiscard]] inline bool read_u32_le(std::istream& in, std::uint32_t& out) noexcept {
  in.read(reinterpret_cast<char*>(&out), sizeof(out));
  return in.gcount() == static_cast<std::streamsize>(sizeof(out));
}

}  // namespace cw::render
