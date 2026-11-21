#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace download::peer::wire {

inline std::uint32_t load_u32_be(std::span<const std::byte> data,
                                 std::size_t offset = 0) {
  return (std::to_integer<std::uint32_t>(data[offset + 0]) << 24) |
         (std::to_integer<std::uint32_t>(data[offset + 1]) << 16) |
         (std::to_integer<std::uint32_t>(data[offset + 2]) << 8) |
         std::to_integer<std::uint32_t>(data[offset + 3]);
}

inline void append_u32_be(std::vector<std::byte> &out, std::uint32_t value) {
  out.push_back(static_cast<std::byte>(value >> 24));
  out.push_back(static_cast<std::byte>((value >> 16) & 0xFF));
  out.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
  out.push_back(static_cast<std::byte>(value & 0xFF));
}

}
