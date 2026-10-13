#pragma once

#include "logger/logger.hxx"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace download::peer {

constexpr uint32_t PIECE_CHUNK_SIZE = 16 * 1024;

struct IncomingPiece {
  uint32_t index;
  uint32_t piece_length;
  std::vector<std::byte> data;
  std::vector<bool> chunk_received;

  IncomingPiece(uint32_t index, uint32_t piece_length)
      : index(index), piece_length(piece_length), data(piece_length),
        chunk_received((piece_length + PIECE_CHUNK_SIZE - 1) / PIECE_CHUNK_SIZE,
                       false) {}

  bool add_chunk(uint32_t offset, std::span<const std::byte> block) {
    if (offset % PIECE_CHUNK_SIZE != 0) {
      logger::warn("add_chunk: offset {} not aligned to chunk size {}", offset,
                   PIECE_CHUNK_SIZE);
      return false;
    }
    if (uint64_t{offset} + block.size() > piece_length) {
      logger::warn("add_chunk: offset {} + block {} > piece_length {}", offset,
                   block.size(), piece_length);
      return false;
    }

    uint32_t expected =
        std::min<uint32_t>(PIECE_CHUNK_SIZE, piece_length - offset);
    if (block.size() != expected) {
      logger::warn("add_chunk: block is {} bytes, expected {}", block.size(),
                   expected);
      return false;
    }

    size_t chunk = offset / PIECE_CHUNK_SIZE;
    if (chunk_received[chunk]) {
      logger::warn("add_chunk: duplicate chunk {}", chunk);
      return false;
    }

    std::copy(block.begin(), block.end(), data.begin() + offset);
    chunk_received[chunk] = true;
    return true;
  }

  bool all_chunks_received() const {
    return std::all_of(chunk_received.begin(), chunk_received.end(),
                       [](bool b) { return b; });
  }
};

} // namespace download::peer
