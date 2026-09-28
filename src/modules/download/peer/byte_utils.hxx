#pragma once

#include <cstdint>
#include <span>
#include <vector>
#include <stdexcept>

namespace download::peer {

inline uint32_t extract_u32(std::span<const std::byte> data, std::size_t offset = 0) {
    if (offset + 4 > data.size()) {
        throw std::out_of_range("byte offset out of range reading u32");
    }
    return (std::to_integer<uint32_t>(data[offset + 0]) << 24) |
           (std::to_integer<uint32_t>(data[offset + 1]) << 16) |
           (std::to_integer<uint32_t>(data[offset + 2]) << 8)  |
            std::to_integer<uint32_t>(data[offset + 3]);
}

inline void append_u32(std::vector<std::byte>& vec, uint32_t value) {
    vec.push_back(static_cast<std::byte>(value >> 24));
    vec.push_back(static_cast<std::byte>((value >> 16) & 0xFF));
    vec.push_back(static_cast<std::byte>((value >> 8) & 0xFF));
    vec.push_back(static_cast<std::byte>(value & 0xFF));
}

inline std::vector<std::byte> u32_to_bytes(uint32_t value) {
    std::vector<std::byte> result;
    append_u32(result, value);
    return result;
}

} // namespace bytes
