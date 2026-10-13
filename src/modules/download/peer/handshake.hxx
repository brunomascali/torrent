#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <span>
#include <string_view>

namespace download::peer {

struct HandshakeMessage {
  static constexpr std::string_view pstr = "BitTorrent protocol";

  static std::array<std::byte, 68>
  as_bytes(std::span<const std::byte, 20> info_hash,
           std::span<const std::byte, 20> peer_id) {
    std::array<std::byte, 68> msg{};
    auto it = msg.begin();

    // pstrlen
    *it++ = static_cast<std::byte>(pstr.size());

    // pstr
    it = std::ranges::transform(pstr, it, [](char c) {
           return static_cast<std::byte>(c);
         }).out;

    // reserved (8 bytes, already zero)
    it += 8;

    // info_hash and peer_id: plain byte copies
    it = std::ranges::copy(info_hash, it).out;
    std::ranges::copy(peer_id, it);

    return msg;
  }
};

} // namespace download::peer
