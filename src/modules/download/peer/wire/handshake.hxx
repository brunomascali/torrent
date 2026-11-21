#pragma once

#include <array>
#include <cstddef>
#include <download/peer/wire/error.hxx>
#include <expected>
#include <span>
#include <string_view>

namespace download::peer::wire {

struct Handshake {
  static constexpr std::string_view protocol = "BitTorrent protocol";
  static constexpr std::size_t size = 1 + protocol.size() + 8 + 20 + 20;

  std::array<std::byte, 8> reserved{};
  std::array<std::byte, 20> info_hash{};
  std::array<std::byte, 20> peer_id{};

  std::array<std::byte, size> encode() const;

  static std::expected<Handshake, Error>
  decode(std::span<const std::byte, size> bytes);

  bool operator==(const Handshake &) const = default;
};

}
