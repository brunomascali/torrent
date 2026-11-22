#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace download::peer {

using PeerId = std::array<std::byte, 20>;

class Swarm {
public:
  virtual ~Swarm() = default;

  virtual bool on_piece(std::uint32_t index, std::span<const std::byte> data) = 0;

  virtual std::optional<std::vector<std::byte>>
  read_block(std::uint32_t index, std::uint32_t begin, std::uint32_t length) = 0;

  virtual bool admit(const PeerId &peer_id) = 0;
  virtual void leave(const PeerId &peer_id) = 0;

  virtual bool acquire_upload_slot() = 0;
  virtual void release_upload_slot() = 0;
};

}
