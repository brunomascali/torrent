#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace download::peer {

class PieceSink {
public:
  virtual ~PieceSink() = default;

  virtual bool on_piece(std::uint32_t index, std::span<const std::byte> data) = 0;
};

}
